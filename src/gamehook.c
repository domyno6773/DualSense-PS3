/*
 * DS45PAD - in-game stick correction.
 *
 * This is a different strategy from the rest of the plugin, and a better one
 * for the problem it solves.
 *
 * Everything else here builds a virtual controller and hopes the game talks to
 * it. That cannot satisfy a game which insists on an official DualShock 3: a
 * virtual pad always reports CELL_PAD_DEV_TYPE_LDD (5), never STANDARD (0), so
 * a game testing the device type rejects it no matter what capability bits are
 * advertised. Hardware adapters get past that check by *being* a DualShock 3 to
 * the console - right vendor id, right HID descriptor, right report format - so
 * the firmware's own driver adopts them. No plugin can do that; we cannot
 * change what a USB device says about itself.
 *
 * What we can do is fix the part the adapters get wrong. They convert the
 * DS4/DualSense sticks to DS3 format linearly, which leaves exactly the corner
 * problem this plugin exists to solve. So: let the adapter handle
 * compatibility, and correct the response curve here, inside the game, by
 * intercepting the call the game uses to read the pad.
 *
 * That works over whatever the game already accepts - an adapter, a real
 * DualShock 3, anything - because it never touches device identity at all.
 *
 * Loaded into a running game process with PS3MAPI (webMAN MOD).
 */

#include <string.h>
#include <cell/pad.h>

#include "ds45pad.h"
#include "hook.h"

static ds45_opd_detour_t getdata_detour;
static ds45_opd_detour_t getdataextra_detour;
static ds45_opd_detour_t getinfo2_detour;

/* The stub table only contains what this module actually imports, so the
 * functions we intend to detour have to be referenced somewhere. Taking their
 * addresses is enough and costs nothing at runtime. */
static void *const keep_imports[] = {
	(void *)cellPadGetData,
	(void *)cellPadGetDataExtra,
	(void *)cellPadGetInfo2,
};

/* ------------------------------------------------------------------------ */
/* Making the pad look official                                              */
/*                                                                           */
/* A game that "requires a DualShock 3" reads cellPadGetInfo2() and checks    */
/* device_type and the capability bits. A virtual pad can never satisfy that: */
/* it always reports CELL_PAD_DEV_TYPE_LDD, and a generic HID pad reports no  */
/* conformity at all. Hardware adapters get around it by being a DualShock 3  */
/* to the console.                                                           */
/*                                                                           */
/* From inside the game process there is a third option: answer the question  */
/* ourselves. The game asks what the pad is, and we say "a conformant         */
/* DualShock 3" - which is the same outcome the adapter buys, without any     */
/* hardware. This is what PS3xPAD's compatibility mode was reaching for by    */
/* patching the game itself; intercepting the API is far less invasive.       */
/* ------------------------------------------------------------------------ */

#define SPOOF_CAPABILITY (DS45_CAP_PS3_CONFORMITY | DS45_CAP_PRESS_MODE | \
                          DS45_CAP_SENSOR_MODE | DS45_CAP_HP_ANALOG | \
                          DS45_CAP_ACTUATOR)

static int getinfo2_hook(CellPadInfo2 *info)
{
	typedef int (*orig_t)(CellPadInfo2 *);
	orig_t orig = (orig_t)(uintptr_t)getinfo2_detour.saved;
	int r, port;

	if (!orig)
		return CELL_PAD_ERROR_UNINITIALIZED;

	r = orig(info);
	if (r != CELL_PAD_OK || !info || !g_cfg.spoof_pad_info)
		return r;

	for (port = 0; port < DS45_MAX_PORT_NUM; port++) {
		if (!(info->port_status[port] & DS45_STATUS_CONNECTED))
			continue;

		info->device_type[port] = DS45_DEV_TYPE_STANDARD;
		info->device_capability[port] |= SPOOF_CAPABILITY;
	}

	return r;
}

/* Apply the response curve to the stick words of a report the game is about to
 * receive. Calibration lives in slot 0; there is only ever one pad being
 * corrected here. */
static void correct_sticks(uint32_t port_no, CellPadData *data)
{
	ds45_pad_t *pad;
	ds45_state_t st;
	uint8_t lx, ly, rx, ry;

	if (!data || data->len <= DS45_BTN_ANALOG_LEFT_Y)
		return;

	/* One calibration per port, not one shared by every port. Two controllers
	 * with different stick hardware would otherwise teach the same min/max,
	 * and the learned range would converge on the union of the two - which is
	 * precisely the too-wide range that costs reach on the diagonals. */
	pad = &g_pads[port_no < DS45_MAX_PADS ? port_no : 0];

	memset(&st, 0, sizeof(st));
	st.lx = (uint8_t)data->button[DS45_BTN_ANALOG_LEFT_X];
	st.ly = (uint8_t)data->button[DS45_BTN_ANALOG_LEFT_Y];
	st.rx = (uint8_t)data->button[DS45_BTN_ANALOG_RIGHT_X];
	st.ry = (uint8_t)data->button[DS45_BTN_ANALOG_RIGHT_Y];
	st.valid = 1;
	st.sticks_valid = 1;

	sticks_transform(pad, &st, &lx, &ly, &rx, &ry);

	data->button[DS45_BTN_ANALOG_LEFT_X]  = lx;
	data->button[DS45_BTN_ANALOG_LEFT_Y]  = ly;
	data->button[DS45_BTN_ANALOG_RIGHT_X] = rx;
	data->button[DS45_BTN_ANALOG_RIGHT_Y] = ry;

	/* Right stick only: that is where the reported dead sector is, and
	 * logging both would halve the useful sample rate for no reason. */
	if (g_cfg.diag)
		diag_sticks(st.rx, st.ry, rx, ry,
		            &pad->cal.axis[DS45_AXIS_RX],
		            &pad->cal.axis[DS45_AXIS_RY]);
}

static int getdata_hook(uint32_t port_no, CellPadData *data)
{
	typedef int (*orig_t)(uint32_t, CellPadData *);
	orig_t orig = (orig_t)(uintptr_t)getdata_detour.saved;
	int r;

	if (!orig)
		return CELL_PAD_ERROR_UNINITIALIZED;

	r = orig(port_no, data);
	if (r == CELL_PAD_OK)
		correct_sticks(port_no, data);

	return r;
}

/* Some titles read through cellPadGetDataExtra instead. */
static int getdataextra_hook(uint32_t port_no, uint32_t *device_type,
                             CellPadData *data)
{
	typedef int (*orig_t)(uint32_t, uint32_t *, CellPadData *);
	orig_t orig = (orig_t)(uintptr_t)getdataextra_detour.saved;
	int r;

	if (!orig)
		return CELL_PAD_ERROR_UNINITIALIZED;

	r = orig(port_no, device_type, data);
	if (r == CELL_PAD_OK)
		correct_sticks(port_no, data);

	return r;
}

int gamehook_install(void)
{
	int n = 0;

	(void)keep_imports;

	/* Where the descriptor lives tells us whether the patch can reach the
	 * game: an address well away from this module means it is the shared one
	 * in sys_io, which every caller in the process goes through. */
	ds45_boot_mark_hex("getdata_opd", (uint32_t)(uintptr_t)cellPadGetData);
	ds45_boot_mark_hex("myhook_opd", (uint32_t)(uintptr_t)getdata_hook);

	if (ds45_detour_opd((void *)cellPadGetData,
	                    (void *)getdata_hook, &getdata_detour) == 0) {
		ds45_boot_mark("hook_cellPadGetData_ok");
		n++;
	} else {
		ds45_boot_mark("hook_cellPadGetData_FAILED");
	}

	if (ds45_detour_opd((void *)cellPadGetDataExtra,
	                    (void *)getdataextra_hook, &getdataextra_detour) == 0) {
		ds45_boot_mark("hook_cellPadGetDataExtra_ok");
		n++;
	}

	if (g_cfg.spoof_pad_info) {
		if (ds45_detour_opd((void *)cellPadGetInfo2,
		                    (void *)getinfo2_hook, &getinfo2_detour) == 0) {
			ds45_boot_mark("hook_cellPadGetInfo2_ok");
			n++;
		} else {
			ds45_boot_mark("hook_cellPadGetInfo2_FAILED");
		}

	}

	return n;
}

void gamehook_remove(void)
{
	ds45_detour_opd_remove(&getdata_detour);
	ds45_detour_opd_remove(&getdataextra_detour);
	ds45_detour_opd_remove(&getinfo2_detour);
}
