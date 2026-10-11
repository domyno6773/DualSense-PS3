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
#include <cell/pad.h>

#include "ds45pad.h"

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
			ds45_notify("DS45PAD: controle Bluetooth adotado");

		bound++;
	}

	return bound;
}

/* Turn the firmware's generic pad sample back into our neutral state so it can
 * go through exactly the same translation path as a USB controller. */
static void state_from_generic(ds45_pad_t *pad, const CellPadData *data,
                               ds45_state_t *st)
{
	uint16_t d1, d2;

	/* Everything here is bounded by data->len, because the buffer was zeroed
	 * before the read and anything the driver did not fill in is therefore 0.
	 *
	 * Reading the stick words unguarded was making the XMB scroll on its own:
	 * a report that only carried the digital buttons left the analog words at
	 * 0, which the response curve then turned into full deflection. It also
	 * poisoned the calibration, teaching it a minimum of 0 on all four axes
	 * from data that never came from the stick. */
	/* Record what the generic driver actually gives us, once. If len never
	 * reaches 8 there are no analog words in the source at all, and no amount
	 * of curve correction can invent them - that would make passthrough
	 * unable to deliver sticks, full stop. */
	{
		static int len_reported;

		if (!len_reported) {
			len_reported = 1;
			ds45_boot_mark_hex("src_pad_len", (uint32_t)data->len);
		}
	}

	if (data->len <= DS45_BTN_DIGITAL2) {
		st->valid = 0;
		return;
	}

	d1 = data->button[DS45_BTN_DIGITAL1];
	d2 = data->button[DS45_BTN_DIGITAL2];

	st->buttons = 0;
	if (d1 & DS45_CTRL_SELECT) st->buttons |= DS45_B_SHARE;
	if (d1 & DS45_CTRL_START)  st->buttons |= DS45_B_OPTIONS;
	if (d1 & DS45_CTRL_L3)     st->buttons |= DS45_B_L3;
	if (d1 & DS45_CTRL_R3)     st->buttons |= DS45_B_R3;
	if (d1 & DS45_CTRL_UP)     st->buttons |= DS45_B_UP;
	if (d1 & DS45_CTRL_RIGHT)  st->buttons |= DS45_B_RIGHT;
	if (d1 & DS45_CTRL_DOWN)   st->buttons |= DS45_B_DOWN;
	if (d1 & DS45_CTRL_LEFT)   st->buttons |= DS45_B_LEFT;

	if (d2 & DS45_CTRL_SQUARE)   st->buttons |= DS45_B_SQUARE;
	if (d2 & DS45_CTRL_CROSS)    st->buttons |= DS45_B_CROSS;
	if (d2 & DS45_CTRL_CIRCLE)   st->buttons |= DS45_B_CIRCLE;
	if (d2 & DS45_CTRL_TRIANGLE) st->buttons |= DS45_B_TRIANGLE;
	if (d2 & DS45_CTRL_L1)       st->buttons |= DS45_B_L1;
	if (d2 & DS45_CTRL_R1)       st->buttons |= DS45_B_R1;
	if (d2 & DS45_CTRL_L2)       st->buttons |= DS45_B_L2;
	if (d2 & DS45_CTRL_R2)       st->buttons |= DS45_B_R2;

	if (data->len > DS45_BTN_ANALOG_LEFT_Y) {
		st->lx = (uint8_t)data->button[DS45_BTN_ANALOG_LEFT_X];
		st->ly = (uint8_t)data->button[DS45_BTN_ANALOG_LEFT_Y];
		st->rx = (uint8_t)data->button[DS45_BTN_ANALOG_RIGHT_X];
		st->ry = (uint8_t)data->button[DS45_BTN_ANALOG_RIGHT_Y];
		st->sticks_valid = 1;
	} else {
		/* No analog words in this report - centre them rather than invent
		 * a deflection, and keep the calibration away from these samples. */
		st->lx = st->ly = st->rx = st->ry = DS45_STICK_CENTER;
		st->sticks_valid = 0;
	}

	/* The generic driver reports the triggers as plain buttons, so synthesise
	 * the analog travel the emulated pressure path expects. */
	st->l2 = (st->buttons & DS45_B_L2) ? 255 : 0;
	st->r2 = (st->buttons & DS45_B_R2) ? 255 : 0;
	if (data->len > DS45_BTN_PRESS_R2) {
		if (data->button[DS45_BTN_PRESS_L2])
			st->l2 = (uint8_t)data->button[DS45_BTN_PRESS_L2];
		if (data->button[DS45_BTN_PRESS_R2])
			st->r2 = (uint8_t)data->button[DS45_BTN_PRESS_R2];
	}

	/* No motion or touch over the generic path. */
	st->gyro[0] = st->gyro[1] = st->gyro[2] = 0;
	st->accel[0] = st->accel[1] = st->accel[2] = 0;
	st->touch[0].active = st->touch[1].active = 0;

	/* PS button. Two ways of getting one:
	 *  - the code/mask pair found with the diagnostic dump, when the firmware
	 *    does surface it somewhere in the report;
	 *  - otherwise a button combination configured by the user. */
	if (g_cfg.bt_ps_code >= 0 && g_cfg.bt_ps_code < data->len &&
	    g_cfg.bt_ps_code < (int)(sizeof(data->button) / sizeof(data->button[0])) &&
	    (data->button[g_cfg.bt_ps_code] & g_cfg.bt_ps_mask))
		st->buttons |= DS45_B_PS;

	/* R3 held means the user is going for the insert-into-game toggle, which
	 * is built from the same two buttons plus R3. Without this exclusion the
	 * substitution below consumed SHARE and OPTIONS first and the toggle
	 * combination could never be recognised at all. */
	if ((st->buttons & (DS45_B_SHARE | DS45_B_OPTIONS | DS45_B_R3)) !=
	    (DS45_B_SHARE | DS45_B_OPTIONS | DS45_B_R3)) {
		switch (g_cfg.bt_ps_combo) {
		case DS45_PSCOMBO_SELECT_START:
			if ((st->buttons & (DS45_B_SHARE | DS45_B_OPTIONS)) ==
			    (DS45_B_SHARE | DS45_B_OPTIONS)) {
				st->buttons |= DS45_B_PS;
				st->buttons &= ~(DS45_B_SHARE | DS45_B_OPTIONS);
			}
			break;
		case DS45_PSCOMBO_L3_R3:
			if ((st->buttons & (DS45_B_L3 | DS45_B_R3)) ==
			    (DS45_B_L3 | DS45_B_R3)) {
				st->buttons |= DS45_B_PS;
				st->buttons &= ~(DS45_B_L3 | DS45_B_R3);
			}
			break;
		default:
			break;
		}
	}

	(void)pad;
	st->valid = 1;
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

void passthru_poll(void)
{
	CellPadData data;
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		ds45_pad_t *pad = &g_pads[i];

		if (!pad->in_use || pad->link != DS45_LINK_BT)
			continue;

		memset(&data, 0, sizeof(data));
		if (cellPadGetData((uint32_t)pad->src_port, &data) != CELL_PAD_OK) {
			/* A read failure is not proof the controller is gone. Only tear
			 * the virtual pad down once the port itself stops reporting as
			 * connected: tearing down and rebuilding on read errors alone
			 * produced a register/unregister cycle every second, which is
			 * far more destabilising than a stale sample. */
			if (++pad->read_fails < PASSTHRU_MAX_READ_FAILS)
				continue;

			pad->read_fails = 0;

			if (source_still_connected(pad->src_port)) {
				/* Reads are failing but the port is still there. The open
				 * question is whether this is what happens while a game
				 * runs - if the VSH loses access to the pad then, no
				 * VSH-side plugin can drive a game, and the work has to
				 * move into the game process. Logged once per episode. */
				if (!pad->read_blocked) {
					pad->read_blocked = 1;
					ds45_boot_mark("src_read_BLOCKED_port_still_connected");
				}
				continue;
			}

			ds45_boot_mark("passthru_source_disconnected");
			ldd_unregister(pad);
			pad->in_use = 0;
			continue;
		}

		pad->read_fails = 0;
		if (pad->read_blocked) {
			pad->read_blocked = 0;
			ds45_boot_mark("src_read_recovered");
		}

		/* len == 0 means "nothing changed since the last read". Inject only
		 * on fresh data: re-sending an unchanged sample every tick is pure
		 * system load, and enough of it stalls apps while they load. */
		if (data.len > 0) {
			if (g_cfg.diag)
				diag_observe(pad->src_port, &data);
			state_from_generic(pad, &data, &pad->state);
			if (!pad->state.valid)
				continue;
			if (g_cfg.diag)
				diag_state(pad);
			ldd_check_combo(pad);
			ldd_inject(pad);
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
