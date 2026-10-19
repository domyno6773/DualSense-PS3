/*
 * DS45PAD - virtual DualShock 3 built on the libpad LDD interface.
 *
 * Syscall 574 requests an LDD pad and capabilities; the granted device type
 * and capabilities must be read back, not assumed. Syscall 573 controls data
 * insertion filtering. The documented LDD PS input is button[0], bit 0.
 * Acceptance of an insertion is not proof that VSH displayed the PS menu.
 */

#include <string.h>
#include <sys/ppu_thread.h>
#include <sys/process.h>
#include <sys/prx.h>
#include <sys/timer.h>
#include <cell/pad.h>

#include "ds45pad.h"

/* sys_io debug pad syscalls. Present on Cobra/Mamba CFW and on DEX kernels;
 * ldd_register() falls back to the plain SDK entry point when they are not. */
static inline void sys_pad_ldd_register(uint8_t *data, int32_t *handle,
                                        uint8_t port, uint32_t capability)
{
	system_call_4(574, (uint64_t)(uintptr_t)data, (uint64_t)(uintptr_t)handle,
	              (uint64_t)port, (uint64_t)capability);
}

static inline int32_t sys_pad_ldd_set_insert_mode(int32_t handle, uint16_t addr,
                                               uint32_t *mode, uint8_t arg)
{
	system_call_4(573, (uint64_t)handle, (uint64_t)addr,
	              (uint64_t)(uintptr_t)mode, (uint64_t)arg);
	return_to_user_prog(int32_t);
}

#define LDD_INSERT_INTO_GAME_ON  1
#define LDD_INSERT_INTO_GAME_OFF 0

/* Explicit insertion policy, never toggled around individual PS samples.
 * Historical hang reports in this repository predate the pointer-argument
 * correction above and do not prove a universal HEN restriction. Retest on
 * the target firmware; keep the conservative startup default OFF. */
int ldd_set_insert_into_game(ds45_pad_t *pad, int on)
{
	uint32_t mode;
	int32_t result;

	if (pad->handle < 0)
		return -1;

	mode = on ? LDD_INSERT_INTO_GAME_ON : LDD_INSERT_INTO_GAME_OFF;
	result = sys_pad_ldd_set_insert_mode(pad->handle, 0x100, &mode, 4);
	if (result != 0) {
		ds45_boot_mark_hex("insert_mode_FAILED", (uint32_t)result);
		return result;
	}
	pad->insert_on = on;

	ds45_boot_mark(on ? "insert_into_game_ON" : "insert_into_game_OFF");
	return 0;
}

/* Lightbar colours matching the PlayStation player convention. */
static const uint8_t player_rgb[4][3] = {
	{ 0x00, 0x00, 0x40 },  /* P1 blue  */
	{ 0x40, 0x00, 0x00 },  /* P2 red   */
	{ 0x00, 0x40, 0x00 },  /* P3 green */
	{ 0x20, 0x00, 0x20 },  /* P4 pink  */
};

/* Number of virtual pads we currently hold. */
static int registered_count(void)
{
	int i, n = 0;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (g_pads[i].handle >= 0)
			n++;

	return n;
}

int ldd_register(ds45_pad_t *pad)
{
	uint8_t scratch[0x114];
	/* volatile: the syscall writes this through the pointer, which the
	 * compiler has no way of knowing. */
	volatile int32_t handle_slot = -1;
	int32_t handle;

	if (pad->handle >= 0)
		return 0;

	/* Single choke point for the pad count, so no caller can run away with it.
	 * This exists because a virtual pad we create shows up as a newly
	 * connected pad, which the Bluetooth scan then adopted as if it were a
	 * physical controller - each adoption creating another pad, until every
	 * slot was gone and the console listed seven controllers. */
	if (registered_count() >= (int)g_cfg.max_pads) {
		ds45_boot_mark("ldd_refused_max_pads");
		return -1;
	}

	memset(scratch, 0, sizeof(scratch));

	/* Third argument is the device type to register as; the capability mask
	 * is shifted before being passed, which is how the reference plugins do
	 * it. Both are configurable because the system does not grant the mask
	 * verbatim, and a game may gate on the device type instead. */
	sys_pad_ldd_register(scratch, (int32_t *)&handle_slot,
	                     (uint8_t)g_cfg.ldd_dev_type,
	                     (uint32_t)g_cfg.capability_mask << g_cfg.capability_shift);

	/* Give the system a moment to publish the new port. */
	sys_timer_usleep(10 * 1000);
	handle = handle_slot;

	if (handle < 0) {
		/* No debug syscalls on this kernel. We still get a working pad,
		 * just without the conformity flag, so PS button and XMB control
		 * work but the strictest games may still refuse it. */
		ds45_boot_mark("ldd_syscall_FAILED_using_fallback");
		handle = cellPadLddRegisterController();
		if (handle < 0) {
			ds45_boot_mark("ldd_fallback_FAILED");
			return -1;
		}
		pad->handle = handle;
	} else {
		ds45_boot_mark_hex("ldd_syscall_ok_handle", (uint32_t)handle);
		pad->handle = handle;

	}
	if (g_cfg.insert_into_game) {
		int res = ldd_set_insert_into_game(pad, 1);
		if (res != 0) {
			ds45_boot_mark_hex("ldd_insert_game_err", (uint32_t)res);
		}
	} else {
		ds45_boot_mark("ldd_insert_into_game_off");
	}

	pad->port = cellPadLddGetPortNo(pad->handle);
	if (pad->port < 0 || pad->port >= DS45_MAX_PORT_NUM) {
		ds45_boot_mark_hex("ldd_invalid_port", (uint32_t)pad->port);
		ldd_unregister(pad);
		return -1;
	}
	ds45_boot_mark_hex("ldd_port", (uint32_t)pad->port);

	/* Read back what the system now advertises for our port. This is the
	 * number that decides whether a game demanding an official DualShock 3
	 * accepts the pad: bit 0 is CELL_PAD_CAPABILITY_PS3_CONFORMITY. */
	{
		CellPadInfo2 info;

		if (cellPadGetInfo2(&info) == CELL_PAD_OK) {
			ds45_boot_mark_hex("ldd_cap",
			                   info.device_capability[pad->port]);
			ds45_boot_mark_hex("ldd_devtype",
			                   info.device_type[pad->port]);
		} else {
			ds45_boot_mark("ldd_getinfo2_FAILED");
		}
	}

	/* Ask for the extended 24 code report: pressure sensitive buttons and
	 * the sixaxis words are only delivered when both are enabled. */
	cellPadSetPortSetting((uint32_t)pad->port,
	                      DS45_SETTING_PRESS_ON | DS45_SETTING_SENSOR_ON);

	pad->out.player = (uint8_t)((pad->port % 4) + 1);
	pad->out.led_r  = player_rgb[pad->port % 4][0];
	pad->out.led_g  = player_rgb[pad->port % 4][1];
	pad->out.led_b  = player_rgb[pad->port % 4][2];
	if (!g_cfg.lightbar)
		pad->out.led_r = pad->out.led_g = pad->out.led_b = 0;
	else if (!g_cfg.dim_lightbar) {
		pad->out.led_r = (uint8_t)(pad->out.led_r ? 0xFF : 0);
		pad->out.led_g = (uint8_t)(pad->out.led_g ? 0xFF : 0);
		pad->out.led_b = (uint8_t)(pad->out.led_b ? 0xFF : 0);
	}
	pad->out.dirty = 1;

	return 0;
}

void ldd_unregister(ds45_pad_t *pad)
{
	if (pad->handle >= 0) {
		if (pad->insert_on)
			ldd_set_insert_into_game(pad, 0);
		cellPadLddUnregisterController(pad->handle);
		pad->handle = -1;
		pad->port   = -1;
	}
}

/* Options + Create + R3 - the DualSense equivalent of START+SELECT+R3, the
 * combination PS3xPAD uses for the same job.
 *
 * This lives here rather than in the USB path so it works on every transport:
 * the first version only checked it while reading a cabled controller, which
 * left no way at all to arm the pad in Bluetooth mode. */
#define COMBO_TOGGLE_INSERT (DS45_B_OPTIONS | DS45_B_SHARE | DS45_B_R3)

/* Samples the combination must be steady before it counts, in either
 * direction. The generic pad's report flickers, so a plain edge test saw one
 * press as several and toggled the mode three times in a row. */
#define COMBO_DEBOUNCE 6

void ldd_check_combo(ds45_pad_t *pad)
{
	int held = (pad->state.buttons & COMBO_TOGGLE_INSERT) == COMBO_TOGGLE_INSERT;
	if (!g_cfg.allow_insert_combo)
		return;

	if (held == pad->combo_held) {
		pad->combo_stable = 0;
		return;
	}

	if (++pad->combo_stable < COMBO_DEBOUNCE)
		return;

	/* State has genuinely changed and stayed changed. */
	pad->combo_stable = 0;
	pad->combo_held = held;

	if (held)
		ldd_set_insert_into_game(pad, !pad->insert_on);
}

/* ------------------------------------------------------------------------ */
/* Translation                                                               */
/* ------------------------------------------------------------------------ */

static int clamp(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static uint16_t sensor_from_accel(int16_t raw, int scale, int invert)
{
	/* DS4/DualSense report 8192 LSB per g. A DualShock 3 sits at 512 with
	 * roughly 113 LSB per g, so the physically correct conversion is
	 * 512 + raw * 113 / 8192. Divide before applying the user scale to keep
	 * the intermediate inside 32 bits. */
	int v = ((int)raw * 113) / 8192;

	v = (v * scale) / 100;
	if (invert)
		v = -v;

	return (uint16_t)clamp(v + DS45_SENSOR_CENTER, 0, 1023);
}

static uint16_t sensor_from_gyro(int16_t raw, int scale, int invert)
{
	/* The gyro scale has no clean physical equivalent on a DS3 - the raw
	 * DS4/DualSense counts are divided down to land inside the 10 bit range
	 * and GYRO_SCALE tunes it from there. */
	int v = ((int)raw) / 32;

	v = (v * scale) / 100;
	if (invert)
		v = -v;

	return (uint16_t)clamp(v + DS45_SENSOR_CENTER, 0, 1023);
}

/* Pressure for a digital button. In RAMP mode the value climbs while the
 * button is held, which gives games that read analog pressure (Metal Gear
 * Solid 4 and friends) something to work with. */
static uint16_t pressure_for(ds45_pad_t *pad, int slot, int held)
{
	uint32_t ticks;

	if (!held) {
		pad->press_ticks[slot] = 0;
		return 0;
	}

	if (g_cfg.pressure == DS45_PRESSURE_DIGITAL)
		return 255;

	ticks = pad->press_ticks[slot] + g_cfg.poll_ms;
	if (ticks > g_cfg.pressure_ramp_ms)
		ticks = g_cfg.pressure_ramp_ms;
	pad->press_ticks[slot] = ticks;

	if (g_cfg.pressure_ramp_ms == 0)
		return 255;

	return (uint16_t)((ticks * 255) / g_cfg.pressure_ramp_ms);
}

void ldd_translate(ds45_pad_t *pad, const ds45_state_t *st, uint16_t *button)
{
	uint16_t d1 = 0, d2 = 0, ldd = 0;
	uint8_t lx, ly, rx, ry;
	uint32_t b = st->buttons;
	int i;

	for (i = 0; i < DS45_PAD_LEN; i++)
		button[i] = 0;

	/* A firmware-owned source already delivers its normal controls. Raw USB
	 * ownership instead needs every control forwarded, even with PS_ONLY=1. */
	if (g_cfg.ps_only && pad->link == DS45_LINK_BT) {
		button[DS45_BTN_LDD] = (b & DS45_B_PS) ? DS45_CTRL_LDD_PS : 0;
		button[DS45_BTN_ANALOG_LEFT_X] = DS45_STICK_CENTER;
		button[DS45_BTN_ANALOG_LEFT_Y] = DS45_STICK_CENTER;
		button[DS45_BTN_ANALOG_RIGHT_X] = DS45_STICK_CENTER;
		button[DS45_BTN_ANALOG_RIGHT_Y] = DS45_STICK_CENTER;
		for (i = DS45_BTN_SENSOR_X; i <= DS45_BTN_SENSOR_G; i++)
			button[i] = DS45_SENSOR_CENTER;
		return;
	}

	/* --- digital buttons ------------------------------------------------ */
	if (b & DS45_B_SHARE)    d1 |= DS45_CTRL_SELECT;
	if (b & DS45_B_OPTIONS)  d1 |= DS45_CTRL_START;
	if (b & DS45_B_L3)       d1 |= DS45_CTRL_L3;
	if (b & DS45_B_R3)       d1 |= DS45_CTRL_R3;
	if (b & DS45_B_UP)       d1 |= DS45_CTRL_UP;
	if (b & DS45_B_RIGHT)    d1 |= DS45_CTRL_RIGHT;
	if (b & DS45_B_DOWN)     d1 |= DS45_CTRL_DOWN;
	if (b & DS45_B_LEFT)     d1 |= DS45_CTRL_LEFT;

	if (b & DS45_B_SQUARE)   d2 |= DS45_CTRL_SQUARE;
	if (b & DS45_B_CROSS)    d2 |= DS45_CTRL_CROSS;
	if (b & DS45_B_CIRCLE)   d2 |= DS45_CTRL_CIRCLE;
	if (b & DS45_B_TRIANGLE) d2 |= DS45_CTRL_TRIANGLE;
	if (b & DS45_B_L1)       d2 |= DS45_CTRL_L1;
	if (b & DS45_B_R1)       d2 |= DS45_CTRL_R1;
	if (b & DS45_B_L2)       d2 |= DS45_CTRL_L2;
	if (b & DS45_B_R2)       d2 |= DS45_CTRL_R2;

	if (b & DS45_B_PS) {
		ldd |= DS45_CTRL_LDD_PS;
	}

	button[DS45_BTN_DIGITAL1] = d1;
	button[DS45_BTN_DIGITAL2] = d2;
	button[DS45_BTN_LDD]      = ldd;

	/* --- remappable extras ---------------------------------------------- */
	if (b & DS45_B_TOUCHPAD) ds45_apply_map(g_cfg.map_touchpad, button);
	if (b & DS45_B_MUTE)     ds45_apply_map(g_cfg.map_mute, button);
	if (b & DS45_B_FN_L)     ds45_apply_map(g_cfg.map_fn_l, button);
	if (b & DS45_B_FN_R)     ds45_apply_map(g_cfg.map_fn_r, button);
	if (b & DS45_B_PADDLE_L) ds45_apply_map(g_cfg.map_paddle_l, button);
	if (b & DS45_B_PADDLE_R) ds45_apply_map(g_cfg.map_paddle_r, button);

	if (g_cfg.touch_mode == DS45_TOUCH_BUTTONS && st->touch[0].active) {
		if (st->touch[0].x < 960)
			ds45_apply_map(g_cfg.map_touch_left, button);
		else
			ds45_apply_map(g_cfg.map_touch_right, button);
	}

	/* --- sticks ---------------------------------------------------------- */
	/* Calibration and the DualShock 3 response curve both live in sticks.c;
	 * see the comment at the top of that file for why raw DS4/DualSense
	 * values are not what a PS3 game expects. */
	sticks_transform(pad, st, &lx, &ly, &rx, &ry);

	if (g_cfg.touch_mode == DS45_TOUCH_RSTICK && st->touch[0].active) {
		/* Touchpad is 1920 wide; height differs between models but the
		 * error is small enough that a single divisor reads fine. */
		rx = (uint8_t)clamp((int)st->touch[0].x * 255 / 1920, 0, 255);
		ry = (uint8_t)clamp((int)st->touch[0].y * 255 / 1000, 0, 255);
	}

	button[DS45_BTN_ANALOG_LEFT_X]  = lx;
	button[DS45_BTN_ANALOG_LEFT_Y]  = ly;
	button[DS45_BTN_ANALOG_RIGHT_X] = rx;
	button[DS45_BTN_ANALOG_RIGHT_Y] = ry;

	/* --- pressure -------------------------------------------------------- */
	button[DS45_BTN_PRESS_UP]       = pressure_for(pad, 0,  b & DS45_B_UP);
	button[DS45_BTN_PRESS_DOWN]     = pressure_for(pad, 1,  b & DS45_B_DOWN);
	button[DS45_BTN_PRESS_LEFT]     = pressure_for(pad, 2,  b & DS45_B_LEFT);
	button[DS45_BTN_PRESS_RIGHT]    = pressure_for(pad, 3,  b & DS45_B_RIGHT);
	button[DS45_BTN_PRESS_TRIANGLE] = pressure_for(pad, 4,  b & DS45_B_TRIANGLE);
	button[DS45_BTN_PRESS_CIRCLE]   = pressure_for(pad, 5,  b & DS45_B_CIRCLE);
	button[DS45_BTN_PRESS_CROSS]    = pressure_for(pad, 6,  b & DS45_B_CROSS);
	button[DS45_BTN_PRESS_SQUARE]   = pressure_for(pad, 7,  b & DS45_B_SQUARE);
	button[DS45_BTN_PRESS_L1]       = pressure_for(pad, 8,  b & DS45_B_L1);
	button[DS45_BTN_PRESS_R1]       = pressure_for(pad, 9,  b & DS45_B_R1);

	/* The triggers are genuinely analog on both controllers, so they never
	 * need emulating. */
	button[DS45_BTN_PRESS_L2] = st->l2;
	button[DS45_BTN_PRESS_R2] = st->r2;

	/* --- motion ---------------------------------------------------------- */
	if (g_cfg.sixaxis) {
		int ax = 0, ay = 1;

		if (g_cfg.swap_xy) { ax = 1; ay = 0; }

		button[DS45_BTN_SENSOR_X] =
			sensor_from_accel(st->accel[ax], g_cfg.accel_scale, g_cfg.invert_x);
		button[DS45_BTN_SENSOR_Y] =
			sensor_from_accel(st->accel[ay], g_cfg.accel_scale, g_cfg.invert_y);
		button[DS45_BTN_SENSOR_Z] =
			sensor_from_accel(st->accel[2], g_cfg.accel_scale, g_cfg.invert_z);
		button[DS45_BTN_SENSOR_G] =
			sensor_from_gyro(st->gyro[1], g_cfg.gyro_scale, g_cfg.invert_g);
	} else {
		button[DS45_BTN_SENSOR_X] = DS45_SENSOR_CENTER;
		button[DS45_BTN_SENSOR_Y] = DS45_SENSOR_CENTER;
		button[DS45_BTN_SENSOR_Z] = DS45_SENSOR_CENTER;
		button[DS45_BTN_SENSOR_G] = DS45_SENSOR_CENTER;
	}
}

void ldd_inject(ds45_pad_t *pad)
{
	CellPadData data;
	int result, ps;

	if (pad->handle < 0 || !pad->state.valid)
		return;

	memset(&data, 0, sizeof(data));
	data.len = DS45_PAD_LEN;

	ldd_translate(pad, &pad->state, (uint16_t *)data.button);

	ps = (data.button[DS45_BTN_LDD] & DS45_CTRL_LDD_PS) != 0;
	if (g_cfg.ps_only && pad->link == DS45_LINK_BT &&
	    pad->ps_seeded && ps == pad->ps_sent && !pad->inject_error)
		return;

	/* Keep insertion mode stable across BOTH press and release. Enabling it
	 * for just the pressed sample can hide release from the system. */
	result = cellPadLddDataInsert(pad->handle, &data);
	if (result != CELL_PAD_OK) {
		if (result != pad->inject_error)
			ds45_boot_mark_hex("ldd_insert_FAILED", (uint32_t)result);
		pad->inject_error = result;
		return;
	}
	pad->inject_error = 0;
	if (g_cfg.diag && (!pad->ps_seeded || ps != pad->ps_sent))
		diag_msg(ps ? "ldd_PS_DOWN_accepted" : "ldd_PS_UP_accepted");
	pad->ps_sent = ps;
	pad->ps_seeded = 1;
}
