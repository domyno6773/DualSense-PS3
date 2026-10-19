/*
 * DS45PAD - internal Bluetooth passthrough.
 *
 * A DualShock 4 or DualSense paired to the console's own Bluetooth radio shows
 * up as a generic HID gamepad: sticks and face buttons work, but the PS button
 * does nothing, there is no pressure sensitivity, no motion, no rumble, and
 * games that check for an official controller reject it.
 *
 * This path receives CellPadData, NOT raw Bluetooth or USB HID reports.
 * It cannot recover a PS bit absent from that data. A configured, measured
 * code/mask can forward a bit the firmware does expose. Legacy substitution
 * combos are opt-in and are not evidence of reading the physical PS button.
 * Raw USB is a separate path and requires successful device acquisition.
 */

#include <string.h>
#include <sys/process.h>
#include <cell/pad.h>

#include "ds45pad.h"

/* Syscall 6 (sys_cfw_peek): passive 64-bit kernel memory read */
static inline uint64_t peekq(uint64_t addr)
{
	system_call_1(6, addr);
	return_to_user_prog(uint64_t);
}

static void ds45_boot_mark_u64(const char *tag, uint64_t val)
{
	char tag_hi[64];
	char tag_lo[64];
	int i = 0;
	while (tag[i] && i < 48) {
		tag_hi[i] = tag[i];
		tag_lo[i] = tag[i];
		i++;
	}
	tag_hi[i] = '_'; tag_hi[i+1] = 'H'; tag_hi[i+2] = 'I'; tag_hi[i+3] = '\0';
	tag_lo[i] = '_'; tag_lo[i+1] = 'L'; tag_lo[i+2] = 'O'; tag_lo[i+3] = '\0';

	ds45_boot_mark_hex(tag_hi, (uint32_t)(val >> 32));
	ds45_boot_mark_hex(tag_lo, (uint32_t)(val & 0xFFFFFFFFULL));
}

/* Consecutive failed reads of the source port before the virtual pad is torn
 * down. At a 10 ms poll this is a fifth of a second of silence. */
#define PASSTHRU_MAX_READ_FAILS 20

/* Ports occupied by pads we created ourselves.
 *
 * Checking device_type alone is not enough: a freshly registered virtual pad
 * does not necessarily report type 5 straight away, and adopting one of our
 * own pads as if it were a physical controller starts a cascade that consumes
 * every slot. The authoritative answer is our own bookkeeping. */
static int port_is_ours(int port)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (g_pads[i].in_use && g_pads[i].handle >= 0 &&
		    g_pads[i].port == port)
			return 1;

	return 0;
}

/* A real DualShock 3 advertises PS3 conformity; a generic Bluetooth HID pad
 * never does. That is the cleanest way to tell the two apart. */
static int is_generic_pad(const CellPadInfo2 *info, int port)
{
	if (!(info->port_status[port] & DS45_STATUS_CONNECTED))
		return 0;
	if (info->device_type[port] == DS45_DEV_TYPE_LDD)
		return 0;
	if (info->device_capability[port] & DS45_CAP_PS3_CONFORMITY)
		return 0;
	if (port_is_ours(port))
		return 0;

	return 1;
}

static int already_bound(int port)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (g_pads[i].in_use && g_pads[i].link == DS45_LINK_BT &&
		    g_pads[i].src_port == port)
			return 1;

	return 0;
}

static ds45_pad_t *alloc_slot(void)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (!g_pads[i].in_use)
			return &g_pads[i];

	return NULL;
}

int passthru_scan(void)
{
	CellPadInfo2 info;
	int port, bound = 0;

	if (cellPadGetInfo2(&info) != CELL_PAD_OK)
		return 0;

	/* Walk downwards: users are told to park the Bluetooth pad on the last
	 * port so the virtual one can take port 1, so the highest matching port
	 * is almost always the right source. */
	for (port = DS45_MAX_PORT_NUM - 1; port >= 0; port--) {
		ds45_pad_t *pad;

		if (g_cfg.bt_source_port >= 0 && port != g_cfg.bt_source_port)
			continue;
		if (!is_generic_pad(&info, port) || already_bound(port))
			continue;

		pad = alloc_slot();
		if (!pad)
			break;

		/* Same as the USB path: keep the calibration across the clear. */
		{
			ds45_stickcal_t keep = pad->cal;

			memset(pad, 0, sizeof(*pad));
			pad->cal = keep;
		}

		pad->in_use   = 1;
		pad->link     = DS45_LINK_BT;
		pad->model    = DS45_MODEL_UNKNOWN;
		pad->src_port = port;
		pad->dev_id   = -1;
		pad->handle   = -1;
		pad->port     = -1;
		pad->c_pipe   = pad->i_pipe = pad->o_pipe = -1;

		/* Only ask for the extended report if the port actually claims to
		 * support it. The generic pad reports capability 0x00 - neither
		 * pressure nor sensors - and forcing both on it was leaving the port
		 * unreadable, which tore the virtual pad down and rebuilt it once a
		 * second. */
		{
			uint32_t want = 0;

			if (info.device_capability[port] & DS45_CAP_PRESS_MODE)
				want |= DS45_SETTING_PRESS_ON;
			if (info.device_capability[port] & DS45_CAP_SENSOR_MODE)
				want |= DS45_SETTING_SENSOR_ON;

			if (want)
				cellPadSetPortSetting((uint32_t)port, want);
		}

		if (ldd_register(pad) < 0) {
			pad->in_use = 0;
			continue;
		}

		if (g_cfg.notify)
			ds45_notify("DS45Pad Bluetooth Attivo / Active");

		bound++;
	}

	return bound;
}



/* Is the source port still listed as connected, regardless of whether reading
 * it happens to be working right now? */
static int source_still_connected(int port)
{
	CellPadInfo2 info;

	if (port < 0 || port >= DS45_MAX_PORT_NUM)
		return 0;
	if (cellPadGetInfo2(&info) != CELL_PAD_OK)
		return 1;   /* cannot tell - assume it is there and keep the pad */

	return (info.port_status[port] & DS45_STATUS_CONNECTED) ? 1 : 0;
}

void ds45_trigger_bt_ps(int pressed)
{
	static int s_last_ps = 0;
	int i;

	if (pressed == s_last_ps)
		return;
	s_last_ps = pressed;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		ds45_pad_t *pad = &g_pads[i];
		CellPadData ldd_data;
		int r, s;

		if (!pad->in_use || pad->link != DS45_LINK_BT || pad->handle < 0)
			continue;

		memset(&ldd_data, 0, sizeof(ldd_data));
		ldd_data.len = DS45_PAD_LEN;
		if (pressed) {
			ldd_data.button[DS45_BTN_LDD]      = DS45_CTRL_LDD_PS;
			ldd_data.button[DS45_BTN_DIGITAL1] = DS45_CTRL_PS;
		}
		ldd_data.button[DS45_BTN_ANALOG_LEFT_X]  = DS45_STICK_CENTER;
		ldd_data.button[DS45_BTN_ANALOG_LEFT_Y]  = DS45_STICK_CENTER;
		ldd_data.button[DS45_BTN_ANALOG_RIGHT_X] = DS45_STICK_CENTER;
		ldd_data.button[DS45_BTN_ANALOG_RIGHT_Y] = DS45_STICK_CENTER;
		for (s = DS45_BTN_SENSOR_X; s <= DS45_BTN_SENSOR_G; s++)
			ldd_data.button[s] = DS45_SENSOR_CENTER;

		r = cellPadLddDataInsert(pad->handle, &ldd_data);
		if (r == CELL_PAD_OK) {
			ds45_boot_mark(pressed ? "BT_PS_LDD_PRESS_OK" : "BT_PS_LDD_RELEASE_OK");
		} else {
			ds45_boot_mark_hex("BT_PS_LDD_FAIL", (uint32_t)r);
		}
	}
}

#define SYSCALL8_OPCODE_PS3MAPI          0x7777
#define PS3MAPI_OPCODE_GET_ALL_PROC_PID  0x0021

static int check_is_game_running(void)
{
	uint32_t pids[16];
	int i, count = 0;
	int32_t ret;

	memset(pids, 0, sizeof(pids));
	system_call_3(8, SYSCALL8_OPCODE_PS3MAPI, PS3MAPI_OPCODE_GET_ALL_PROC_PID, (uint64_t)(uintptr_t)pids);
	ret = (int32_t)p1;

	if (ret == 0) {
		for (i = 0; i < 16; i++) {
			if (pids[i] != 0)
				count++;
		}
		return (count > 1) ? 1 : 0;
	}
	return -1; /* error / transient: keep current state */
}

static inline uint8_t deadzone8(uint8_t val)
{
	if (val >= 123 && val <= 133)
		return 128;
	return val;
}

void ds45_inject_bt_pad(uint64_t r_w0, uint64_t r_w1)
{
	static int s_last_ps = 0;
	static int s_ps_grace = 0;
	static int s_neutral_sent = 0;
	static int s_is_game_cached = 0;
	static int s_game_check_tick = 0;
	static int s_game_streak = 0;
	static int s_vsh_streak = 0;
	static int s_logged_state = -1;
	static int s_vsh_menu_open = 0;
	static int s_ps_held_ticks = 0;
	static int s_menu_opened_by_this_hold = 0;
	static int s_menu_timer = 0;
	static int s_last_circle = 0;
	CellPadInfo2 pinfo;
	int intercepted = 0;
	int in_game = 0;
	int circle_pressed = 0;
	int i;

	/* Extract bytes from raw report in kernel bt_work */
	uint8_t lx = (uint8_t)(r_w0 >> 40);
	uint8_t ly = (uint8_t)(r_w0 >> 32);
	uint8_t rx = (uint8_t)(r_w0 >> 24);
	uint8_t ry = (uint8_t)(r_w0 >> 16);
	uint8_t b0 = (uint8_t)(r_w0 >> 8);  /* D-pad hat + Square/Cross/Circle/Triangle */
	uint8_t b1 = (uint8_t)(r_w0);       /* L1/R1/L2/R2/Create/Options/L3/R3 */
	uint8_t b2 = (uint8_t)(r_w1 >> 56); /* PS/Touchpad/Mute */
	uint8_t l2_analog = (uint8_t)(r_w1 >> 48);
	uint8_t r2_analog = (uint8_t)(r_w1 >> 40);

	int ps_phys = (b2 & 0x01) ? 1 : 0;
	uint16_t d1 = 0;
	uint16_t d2 = 0;
	uint16_t ldd = 0;

	/* Check if the system is intercepting pad inputs (in-game XMB active over a running title) */
	if (cellPadGetInfo2(&pinfo) == CELL_PAD_OK) {
		intercepted = (pinfo.system_info & 0x01) ? 1 : 0; /* bit 0 = CELL_PAD_INFO_INTERCEPTED */
	}

	/* Periodically check if a game/app process is running (every ~100ms) */
	if (++s_game_check_tick >= 10) {
		s_game_check_tick = 0;
		int g = check_is_game_running();
		if (g >= 0) {
			if (g == 1) {
				s_game_streak++;
				s_vsh_streak = 0;
				if (s_game_streak >= 1)
					s_is_game_cached = 1;
			} else {
				s_vsh_streak++;
				s_game_streak = 0;
				/* Require 5 consecutive VSH detections (~500ms) to exit game mode */
				if (s_vsh_streak >= 5)
					s_is_game_cached = 0;
			}
		}
	}

	in_game = s_is_game_cached || intercepted;

	if (in_game != s_logged_state) {
		s_logged_state = in_game;
		ds45_boot_mark(in_game ? "STATE_IN_GAME" : "STATE_IN_VSH");
		if (in_game) {
			s_vsh_menu_open = 0;
			for (i = 0; i < DS45_MAX_PADS; i++) {
				if (g_pads[i].in_use && g_pads[i].handle >= 0)
					ldd_set_insert_into_game(&g_pads[i], 1);
			}
		}
	}

	/* Physical PS button */
	if (ps_phys) {
		ldd |= DS45_CTRL_LDD_PS;
		d1  |= DS45_CTRL_PS;
		s_ps_held_ticks++;
		if (!in_game && s_ps_held_ticks >= 25 && !s_vsh_menu_open) {
			s_vsh_menu_open = 1;
			s_menu_opened_by_this_hold = 1;
			s_menu_timer = 3000; /* 30 seconds */
			ds45_boot_mark("VSH_MENU_OPEN");
		}
	} else {
		s_ps_held_ticks = 0;
	}

	if (ps_phys != s_last_ps) {
		s_last_ps = ps_phys;
		ds45_boot_mark(ps_phys ? "BT_PS_LDD_PRESS_OK" : "BT_PS_LDD_RELEASE_OK");

		if (!ps_phys) {
			if (s_menu_opened_by_this_hold) {
				/* User finished holding PS button to open menu. Keep menu open! */
				s_menu_opened_by_this_hold = 0;
			} else if (!in_game && s_vsh_menu_open) {
				/* PS button pressed and released again while menu was open: close menu */
				s_vsh_menu_open = 0;
				s_neutral_sent = 0;
				ds45_boot_mark("VSH_MENU_CLOSE_PS");
			}
		}
	}

	/* D-pad Hat decode (b0 low 4 bits: 0=Up, 1=UR, 2=R, 3=DR, 4=D, 5=DL, 6=L, 7=UL, 8=neutral) */
	uint8_t hat = b0 & 0x0F;
	if (hat == 0 || hat == 1 || hat == 7) d1 |= DS45_CTRL_UP;
	if (hat == 1 || hat == 2 || hat == 3) d1 |= DS45_CTRL_RIGHT;
	if (hat == 3 || hat == 4 || hat == 5) d1 |= DS45_CTRL_DOWN;
	if (hat == 5 || hat == 6 || hat == 7) d1 |= DS45_CTRL_LEFT;

	/* Action buttons (b0 high 4 bits) */
	if (b0 & 0x10) d2 |= DS45_CTRL_SQUARE;
	if (b0 & 0x20) d2 |= DS45_CTRL_CROSS;
	if (b0 & 0x40) d2 |= DS45_CTRL_CIRCLE;
	if (b0 & 0x80) d2 |= DS45_CTRL_TRIANGLE;

	/* Shoulder / System buttons (b1) */
	if (b1 & 0x01) d2 |= DS45_CTRL_L1;
	if (b1 & 0x02) d2 |= DS45_CTRL_R1;
	if (b1 & 0x04) d2 |= DS45_CTRL_L2;
	if (b1 & 0x08) d2 |= DS45_CTRL_R2;
	if (b1 & 0x10) d1 |= DS45_CTRL_SELECT; /* Share / Create */
	if (b1 & 0x20) d1 |= DS45_CTRL_START;  /* Options */
	if (b1 & 0x40) d1 |= DS45_CTRL_L3;
	if (b1 & 0x80) d1 |= DS45_CTRL_R3;

	circle_pressed = (d2 & DS45_CTRL_CIRCLE) ? 1 : 0;

	/* Handle VSH Menu cancel/confirm/timeout */
	if (!in_game && s_vsh_menu_open) {
		if (s_menu_timer > 0)
			s_menu_timer--;

		if (s_last_circle && !circle_pressed) {
			/* Circle released: user canceled menu */
			s_vsh_menu_open = 0;
			s_neutral_sent = 0;
			ds45_boot_mark("VSH_MENU_CLOSE_CIRCLE");
		} else if (d2 & DS45_CTRL_CROSS) {
			/* Cross pressed: user confirmed an action, keep open 20s for Yes/No dialog */
			s_menu_timer = 2000;
		} else if (s_menu_timer == 0) {
			/* Menu timed out */
			s_vsh_menu_open = 0;
			s_neutral_sent = 0;
			ds45_boot_mark("VSH_MENU_TIMEOUT");
		}
	}
	s_last_circle = circle_pressed;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		ds45_pad_t *pad = &g_pads[i];
		CellPadData ldd_data;
		int s;
		int should_gate = 0;

		if (!pad->in_use || pad->link != DS45_LINK_BT || pad->handle < 0)
			continue;

		/* In VSH (dashboard, no game running):
		 * The physical controller on pad->src_port is ALREADY communicating directly with VSH.
		 * If we inject duplicate sticks and buttons into VSH via the virtual pad, VSH
		 * receives every input TWICE (double input).
		 * Therefore, in VSH, the virtual pad MUST be gated (silent):
		 * It only injects the PS button when ps_phys is pressed (or during release).
		 * EXCEPTION: When the PS menu is opened in VSH (s_vsh_menu_open == 1),
		 * the PS menu modal dialog ONLY accepts confirmation (X) and cancel (O) from Player 1 (Port 0).
		 * So when s_vsh_menu_open is 1, un-gate so Player 1 can confirm or cancel!
		 *
		 * In a game (in_game == 1):
		 * Games ignore the physical pad on Port 6 (Controller 7) and ONLY listen to Player 1 on Port 0.
		 * Therefore, in a game, if pad->port == 0, we MUST inject full controller data!
		 * If pad->port > 0 in a game, gate it unless in-game XMB is intercepted. */
		if (!in_game) {
			if (!s_vsh_menu_open)
				should_gate = 1;
		} else if (pad->port > 0 && !intercepted) {
			should_gate = 1;
		}

		if (should_gate && !ps_phys && s_ps_grace == 0) {
			if (s_neutral_sent)
				continue;
			s_neutral_sent = 1;

			memset(&ldd_data, 0, sizeof(ldd_data));
			ldd_data.len = DS45_PAD_LEN;
			ldd_data.button[DS45_BTN_ANALOG_LEFT_X]  = DS45_STICK_CENTER;
			ldd_data.button[DS45_BTN_ANALOG_LEFT_Y]  = DS45_STICK_CENTER;
			ldd_data.button[DS45_BTN_ANALOG_RIGHT_X] = DS45_STICK_CENTER;
			ldd_data.button[DS45_BTN_ANALOG_RIGHT_Y] = DS45_STICK_CENTER;
			for (s = DS45_BTN_SENSOR_X; s <= DS45_BTN_SENSOR_G; s++)
				ldd_data.button[s] = DS45_SENSOR_CENTER;
			cellPadLddDataInsert(pad->handle, &ldd_data);
			continue;
		}

		s_neutral_sent = 0;

		memset(&ldd_data, 0, sizeof(ldd_data));
		ldd_data.len = DS45_PAD_LEN;

		ldd_data.button[DS45_BTN_LDD]            = ldd;
		ldd_data.button[DS45_BTN_DIGITAL1]       = should_gate ? (ps_phys ? DS45_CTRL_PS : 0) : d1;
		ldd_data.button[DS45_BTN_DIGITAL2]       = should_gate ? 0 : d2;
		ldd_data.button[DS45_BTN_ANALOG_LEFT_X]  = should_gate ? DS45_STICK_CENTER : deadzone8(lx);
		ldd_data.button[DS45_BTN_ANALOG_LEFT_Y]  = should_gate ? DS45_STICK_CENTER : deadzone8(ly);
		ldd_data.button[DS45_BTN_ANALOG_RIGHT_X] = should_gate ? DS45_STICK_CENTER : deadzone8(rx);
		ldd_data.button[DS45_BTN_ANALOG_RIGHT_Y] = should_gate ? DS45_STICK_CENTER : deadzone8(ry);

		/* Digital pressure values */
		if (!should_gate) {
			ldd_data.button[DS45_BTN_PRESS_RIGHT]    = (d1 & DS45_CTRL_RIGHT) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_LEFT]     = (d1 & DS45_CTRL_LEFT) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_UP]       = (d1 & DS45_CTRL_UP) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_DOWN]     = (d1 & DS45_CTRL_DOWN) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_TRIANGLE] = (d2 & DS45_CTRL_TRIANGLE) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_CIRCLE]   = (d2 & DS45_CTRL_CIRCLE) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_CROSS]    = (d2 & DS45_CTRL_CROSS) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_SQUARE]   = (d2 & DS45_CTRL_SQUARE) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_L1]       = (d2 & DS45_CTRL_L1) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_R1]       = (d2 & DS45_CTRL_R1) ? 255 : 0;
			ldd_data.button[DS45_BTN_PRESS_L2]       = l2_analog;
			ldd_data.button[DS45_BTN_PRESS_R2]       = r2_analog;
		}

		for (s = DS45_BTN_SENSOR_X; s <= DS45_BTN_SENSOR_G; s++)
			ldd_data.button[s] = DS45_SENSOR_CENTER;

		cellPadLddDataInsert(pad->handle, &ldd_data);
	}
}

void ds45_process_bt_input(uint32_t port_no, CellPadData *data)
{
	static int s_mask_ss_frames = 0;
	static int s_mask_lr_frames = 0;
	static uint16_t s_last_d1 = 0;
	uint16_t d1;
	int combo_select_start = 0;
	int combo_l3_r3 = 0;
	int ps_pressed = 0;

	if (!data || data->len <= DS45_BTN_DIGITAL1)
		return;

	d1 = data->button[DS45_BTN_DIGITAL1];

	if (d1 != s_last_d1) {
		s_last_d1 = d1;
		ds45_boot_mark_hex("BT_HOOK_D1", (uint32_t)d1);
	}

	/* Check physical PS button bit in case exposed */
	if ((d1 & DS45_CTRL_PS) || (data->button[DS45_BTN_LDD] & DS45_CTRL_LDD_PS))
		ps_pressed = 1;

	/* Check configured custom code/mask */
	if (g_cfg.bt_ps_code >= 0 && g_cfg.bt_ps_code < data->len &&
	    g_cfg.bt_ps_code < (int)(sizeof(data->button) / sizeof(data->button[0])) &&
	    (data->button[g_cfg.bt_ps_code] & g_cfg.bt_ps_mask))
		ps_pressed = 1;

	/* Combo 1: CREATE + OPTIONS (SELECT + START) */
	if ((d1 & (DS45_CTRL_SELECT | DS45_CTRL_START)) == (DS45_CTRL_SELECT | DS45_CTRL_START)) {
		combo_select_start = 1;
		ps_pressed = 1;
		s_mask_ss_frames = 12; /* keep masking for ~200ms on release */
	}

	/* Combo 2: L3 + R3 */
	if ((d1 & (DS45_CTRL_L3 | DS45_CTRL_R3)) == (DS45_CTRL_L3 | DS45_CTRL_R3)) {
		combo_l3_r3 = 1;
		ps_pressed = 1;
		s_mask_lr_frames = 12;
	}

	/* Swallow combo buttons while pressed and during release cooldown so they don't leak into game/XMB */
	if (combo_select_start || s_mask_ss_frames > 0) {
		data->button[DS45_BTN_DIGITAL1] &= ~(DS45_CTRL_SELECT | DS45_CTRL_START);
		if (!combo_select_start && s_mask_ss_frames > 0)
			s_mask_ss_frames--;
	}

	if (combo_l3_r3 || s_mask_lr_frames > 0) {
		data->button[DS45_BTN_DIGITAL1] &= ~(DS45_CTRL_L3 | DS45_CTRL_R3);
		if (!combo_l3_r3 && s_mask_lr_frames > 0)
			s_mask_lr_frames--;
	}

	if (ps_pressed) {
		data->button[DS45_BTN_LDD]      |= DS45_CTRL_LDD_PS;
		data->button[DS45_BTN_DIGITAL1] |= DS45_CTRL_PS;
		ds45_trigger_bt_ps(1);
	} else {
		ds45_trigger_bt_ps(0);
	}

	(void)port_no;
}

void passthru_poll(void)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		ds45_pad_t *pad = &g_pads[i];

		if (!pad->in_use || pad->link != DS45_LINK_BT)
			continue;

		if (!source_still_connected(pad->src_port)) {
			ds45_boot_mark("passthru_source_disconnected");
			ldd_unregister(pad);
			pad->in_use = 0;
			continue;
		}

		/* Internal Bluetooth: Live Kernel Memory Monitoring via Syscall 6 (sys_cfw_peek) */
		{
			static int s_bt_init_done = 0;
			static uint64_t s_bt_work_addr = 0;

			if (!s_bt_init_done) {
				s_bt_init_done = 1;
				/* FW 4.93 TOC = 0x34FBB0, bt_rtoc_entry_1 = -0x3680 */
				s_bt_work_addr = peekq(0x800000000034FBB0ULL - 0x3680ULL);
				ds45_boot_mark_u64("PEEK_BT_WORK", s_bt_work_addr);
			}

			if ((s_bt_work_addr & 0xFFFFFFFF00000000ULL) == 0x8000000000000000ULL &&
			    s_bt_work_addr > 0x8000000000010000ULL) {
				uint64_t r_w0 = peekq(s_bt_work_addr + 0xE8);
				uint64_t r_w1 = peekq(s_bt_work_addr + 0xF0);

				/* Verify Bluetooth HID input report (0xA1 prefix) */
				if ((r_w0 >> 56) == 0xA1) {
					/* Forward complete controller report (including physical PS button) to LDD pad */
					ds45_inject_bt_pad(r_w0, r_w1);
				}
			}
		}
	}
}

void passthru_release(void)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		if (g_pads[i].in_use && g_pads[i].link == DS45_LINK_BT) {
			ldd_unregister(&g_pads[i]);
			g_pads[i].in_use = 0;
		}
	}
}
