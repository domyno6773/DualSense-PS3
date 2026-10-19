/*
 * DS45PAD - full DualShock 4 / DualSense support for the PlayStation 3.
 *
 * Build twice from the same sources:
 *
 *   ds45pad_vsh.sprx    loaded by the CFW plugin loader, owns the pad in the
 *                       XMB and keeps owning it while games run
 *   ds45pad_game.sprx   loaded into a running game process (PS3MAPI or Cobra).
 *                       Corrects the stick response curve and answers the
 *                       game's cellPadGetInfo2 - see docs/ARQUITETURA.md.
 *                       Not a rumble build: rumble is out of scope.
 *
 * Licensed under GPLv2 - see LICENSE.
 */

#include <string.h>
#include <sys/ppu_thread.h>
#include <sys/process.h>
#include <sys/prx.h>
#include <sys/timer.h>
#include <cell/pad.h>

#include "ds45pad.h"
#include "hook.h"

int ds45_start(uint64_t arg);
int ds45_stop(void);

SYS_MODULE_INFO(DS45PAD, 0, 1, 0);
SYS_MODULE_START(ds45_start);
SYS_MODULE_STOP(ds45_stop);

#define THREAD_NAME      "ds45pad"
#define STOP_THREAD_NAME "ds45stop"
#define THREAD_PRIO      1000
#define THREAD_STACK     0x4000

/* How often to look for newly paired Bluetooth pads, and to refresh the
 * lightbar after a port reassignment. */
#define RESCAN_MS        1000
/* Diagnostic sampling period - must be well under a button press. */
#define DIAG_MS          20
/* How long to wait before the "loaded" notification, so it does not land
 * while the XMB is still building itself. */
#define ANNOUNCE_MS      8000

/* g_cfg lives in config.c next to its parser. */
ds45_pad_t g_pads[DS45_MAX_PADS];

static sys_ppu_thread_t thread_id = (sys_ppu_thread_t)-1;
static volatile int     running;

#ifndef DS45_GAME

/* ------------------------------------------------------------------------ */
/* Rumble                                                                    */
/*                                                                           */
/* Games ask for vibration with cellPadSetActDirect(), which the firmware     */
/* routes to a physical pad we do not own. The only way to see the request is */
/* to be inside the calling process, so the in-game build detours the import  */
/* and records what the game asked for; the poll loop then forwards it to the */
/* real controller over USB.                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
	uint8_t motor[2];    /* [0] small/high frequency, [1] large/low frequency */
	uint8_t reserved[6];
} ds45_act_param_t;

static ds45_detour_t  act_detour;
static uint8_t        act_small[DS45_MAX_PORT_NUM];
static uint8_t        act_large[DS45_MAX_PORT_NUM];

static int act_direct_hook(uint32_t port_no, ds45_act_param_t *param)
{
	typedef int (*orig_t)(uint32_t, ds45_act_param_t *);
	orig_t orig = (orig_t)(uintptr_t)act_detour.saved;

	if (param && port_no < DS45_MAX_PORT_NUM) {
		act_small[port_no] = param->motor[0];
		act_large[port_no] = param->motor[1];
	}

	return orig ? orig(port_no, param) : 0;
}

static void rumble_install(void)
{
	if (!g_cfg.rumble)
		return;

	ds45_detour("sys_io", NID_CELL_PAD_SET_ACT_DIRECT,
	            (void *)act_direct_hook, &act_detour);
}

static void rumble_remove(void)
{
	ds45_detour_remove(&act_detour);
}

/* ------------------------------------------------------------------------ */
/* VSH Pad Input Interception (Bluetooth Passthrough)                       */
/* ------------------------------------------------------------------------ */

static ds45_opd_detour_t getdata_detour;
static ds45_opd_detour_t getdataextra_detour;

static void *const keep_vsh_imports[] = {
	(void *)cellPadGetData,
	(void *)cellPadGetDataExtra,
};

static int is_bt_source_port(uint32_t port_no)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		if (g_pads[i].in_use && g_pads[i].link == DS45_LINK_BT &&
		    g_pads[i].handle >= 0 && g_pads[i].src_port == (int)port_no)
			return 1;
	}

	return 0;
}

static void silence_pad_data(CellPadData *data)
{
	int s;

	if (!data)
		return;

	for (s = 0; s < data->len && s < CELL_PAD_MAX_CODES; s++)
		data->button[s] = 0;

	if (data->len > DS45_BTN_ANALOG_LEFT_Y) {
		data->button[DS45_BTN_ANALOG_LEFT_X]  = DS45_STICK_CENTER;
		data->button[DS45_BTN_ANALOG_LEFT_Y]  = DS45_STICK_CENTER;
		data->button[DS45_BTN_ANALOG_RIGHT_X] = DS45_STICK_CENTER;
		data->button[DS45_BTN_ANALOG_RIGHT_Y] = DS45_STICK_CENTER;
	}

	for (s = DS45_BTN_SENSOR_X; s <= DS45_BTN_SENSOR_G && s < data->len; s++)
		data->button[s] = DS45_SENSOR_CENTER;
}

static int vsh_getdata_hook(uint32_t port_no, CellPadData *data)
{
	typedef int (*orig_t)(uint32_t, CellPadData *);
	orig_t orig = (orig_t)(uintptr_t)getdata_detour.saved;
	int r;

	if (!orig)
		return CELL_PAD_ERROR_UNINITIALIZED;

	r = orig(port_no, data);
	if (r == CELL_PAD_OK && data && data->len > 0) {
		if (port_no > 0 && is_bt_source_port(port_no))
			silence_pad_data(data);
	}

	return r;
}

static int vsh_getdataextra_hook(uint32_t port_no, uint32_t *device_type,
                                 CellPadData *data)
{
	typedef int (*orig_t)(uint32_t, uint32_t *, CellPadData *);
	orig_t orig = (orig_t)(uintptr_t)getdataextra_detour.saved;
	int r;

	if (!orig)
		return CELL_PAD_ERROR_UNINITIALIZED;

	r = orig(port_no, device_type, data);
	if (r == CELL_PAD_OK && data && data->len > 0) {
		if (port_no > 0 && is_bt_source_port(port_no))
			silence_pad_data(data);
	}

	return r;
}

static void vsh_hooks_install(void)
{
	ds45_detour_opd((void *)cellPadGetData,
	                (void *)vsh_getdata_hook, &getdata_detour);
	ds45_detour_opd((void *)cellPadGetDataExtra,
	                (void *)vsh_getdataextra_hook, &getdataextra_detour);
	ds45_boot_mark("vsh_hooks_installed");
}

static void vsh_hooks_remove(void)
{
	ds45_detour_opd_remove(&getdata_detour);
	ds45_detour_opd_remove(&getdataextra_detour);
}

/* Push whatever the game last asked for to each controller. Only send when
 * something actually changed - the controllers do not need a stream of
 * identical output reports and the DualSense in particular dislikes it. */
static void rumble_update(void)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		ds45_pad_t *pad = &g_pads[i];
		uint8_t weak, strong;

		if (!pad->in_use || pad->port < 0 || pad->port >= DS45_MAX_PORT_NUM)
			continue;

		if (!g_cfg.rumble) {
			weak = strong = 0;
		} else {
			/* The small motor is on/off on a DualShock 3; both DS4 and
			 * DualSense take a magnitude, so a request for "on" becomes
			 * full scale. */
			weak   = act_small[pad->port] ? 0xFF : 0x00;
			strong = act_large[pad->port];
		}

		if (weak == pad->out.rumble_weak && strong == pad->out.rumble_strong)
			continue;

		pad->out.rumble_weak   = weak;
		pad->out.rumble_strong = strong;
		pad->out.dirty         = 1;
	}
}

/* ------------------------------------------------------------------------ */
/* Housekeeping                                                              */
/* ------------------------------------------------------------------------ */

/* The system reshuffles ports when controllers come and go; when that
 * happens the player indicator has to follow. */
static void refresh_ports(void)
{
	int i;

	for (i = 0; i < DS45_MAX_PADS; i++) {
		ds45_pad_t *pad = &g_pads[i];
		int port;

		if (!pad->in_use || pad->handle < 0)
			continue;

		port = cellPadLddGetPortNo(pad->handle);
		if (port < 0 || port == pad->port)
			continue;

		pad->port = port;
		pad->out.player = (uint8_t)((port % 4) + 1);
		pad->out.dirty  = 1;
	}
}

/* Dump every connected port once, into the boot trace rather than the debug
 * log, so it is always available without turning diagnostics on.
 *
 * This is what tells us whether the firmware's own driver still holds the
 * controller on a lower port than our virtual pad: the XMB reacts to the pad
 * on the lowest port, so a generic pad sitting on port 0 would swallow the PS
 * button no matter what we inject further up. */
static void dump_ports_once(void)
{
	static int done;
	CellPadInfo2 info;
	int port;

	if (done)
		return;
	done = 1;

	if (cellPadGetInfo2(&info) != CELL_PAD_OK) {
		ds45_boot_mark("ports_getinfo2_FAILED");
		return;
	}

	ds45_boot_mark_hex("ports_now_connect", info.now_connect);

	for (port = 0; port < DS45_MAX_PORT_NUM; port++) {
		if (!(info.port_status[port] & DS45_STATUS_CONNECTED))
			continue;

		/* One packed line per port: type in the high byte, capability in
		 * the low byte. type 5 = our LDD pad, 0 = a standard pad. */
		ds45_boot_mark_hex(
			(port == 0) ? "port0_type_cap" :
			(port == 1) ? "port1_type_cap" :
			(port == 2) ? "port2_type_cap" :
			(port == 3) ? "port3_type_cap" :
			(port == 4) ? "port4_type_cap" :
			(port == 5) ? "port5_type_cap" : "port6_type_cap",
			(info.device_type[port] << 8) |
			(info.device_capability[port] & 0xFF));
	}
}

#endif /* !DS45_GAME */

static void pads_reset(void)
{
	int i;

	memset(g_pads, 0, sizeof(g_pads));
	for (i = 0; i < DS45_MAX_PADS; i++) {
		g_pads[i].handle   = -1;
		g_pads[i].port     = -1;
		g_pads[i].src_port = -1;
		g_pads[i].dev_id   = -1;
		g_pads[i].c_pipe   = -1;
		g_pads[i].i_pipe   = -1;
		g_pads[i].o_pipe   = -1;
	}
}

static void ds45_thread(uint64_t arg)
{
#ifdef DS45_GAME
	uint32_t game_tick = 0;
#else
	uint32_t since_rescan = 0;
	uint32_t since_diag = 0;
	uint32_t uptime = 0;
	int announced = 0;
	int usb_ok;
#endif

	(void)arg;

	/* Startup is traced to a file, one marker written *before* each step that
	 * touches the system. If the console ever hangs while loading the plugin,
	 * the last marker in that file names the step that did it. */
	ds45_boot_mark("start");
	ds45_boot_mark(DS45PAD_VERSION);

	ds45_config_defaults(&g_cfg);
	ds45_config_load(&g_cfg, DS45_CONFIG_FILE);
	ds45_boot_mark("config");

	if (!g_cfg.enable) {
		/* Loaded, and deliberately inert. Nothing below this point runs, so
		 * if the console still misbehaves the module itself is not the
		 * cause. */
		ds45_boot_mark("DISABLED_by_config");
		sys_ppu_thread_exit(0);
		return;
	}

	pads_reset();
	sticks_load();
	ds45_boot_mark("sticks");

#ifdef DS45_GAME
	/* Inside a game process the job is different and much smaller: correct the
	 * stick curve on the way to the game and touch nothing else. No virtual
	 * pad, so no device-type problem - see gamehook.c.
	 *
	 * Nothing here may touch libusbd. A game process does not load it - the
	 * module list of a running title has libio and libfs but no libusbd - so
	 * an import of it leaves the loader unable to resolve the module, and
	 * PS3MAPI just sits there loading forever. */
	ds45_boot_mark("game_mode");

	/* Slot 0 holds the calibration the game hook corrects against. Claim it:
	 * sticks_save_if_dirty() only considers slots in use, and in this build
	 * nothing else ever claims one - there is no USB or Bluetooth attach path
	 * here - so without this the calibration is learned every session and
	 * written none of them. */
	g_pads[0].in_use = 1;

	gamehook_install();

	while (running) {
		sys_timer_usleep(200 * 1000);

		/* Five ticks to the second, matching what the rate limit inside
		 * sticks_save_if_dirty() assumes. */
		if (g_cfg.stick_calibrate && ++game_tick >= 5) {
			game_tick = 0;
			sticks_save_if_dirty(0);
		}
	}

	gamehook_remove();

	/* Restoring the descriptor does not evict a game thread already executing
	 * inside the hook, and the module is freed as soon as ds45_stop() returns.
	 * Without a pause here, unloading the plugin while the game is polling the
	 * pad can leave a thread running in memory that has just been released. */
	sys_timer_usleep(300 * 1000);

	if (g_cfg.stick_calibrate)
		sticks_save_if_dirty(1);
	sys_ppu_thread_exit(0);

#else

	/* No cellSysmoduleLoadModule() here on purpose: libusbd is already up in
	 * the VSH process, and importing sysmodule pulls a stub the VSH may not
	 * resolve at load time - which hangs the console instead of failing. */
	if (g_cfg.mode != DS45_MODE_BT) {
		ds45_boot_mark("usb_init");
		usb_ok = (usb_init() == 0);
	} else {
		usb_ok = 0;
	}
	ds45_boot_mark(usb_ok ? "usb_init_OK" : "usb_inactive_or_failed");

	rumble_install();
	ds45_boot_mark("rumble");

	vsh_hooks_install();

	if (g_cfg.diag) {
		diag_msg("=== DS45PAD " DS45PAD_VERSION " ===");
		diag_dump_ports();
	}

	ds45_boot_mark("loop");
	while (running) {
		sys_timer_usleep(g_cfg.poll_ms * 1000);

		if (usb_ok)
			usb_poll();

		if (g_cfg.mode != DS45_MODE_USB)
			passthru_poll();

		rumble_update();

		/* Announce once the XMB has had time to come up. Waiting here rather
		 * than before the loop keeps controllers alive from the first tick. */
		uptime += g_cfg.poll_ms;
		if (!announced && uptime >= ANNOUNCE_MS) {
			announced = 1;
			if (g_cfg.notify) {
				ds45_boot_mark("notify");
				ds45_notify("DS45Pad Caricato / Loaded");
				ds45_boot_mark("notify_ok");
			}
		}

		/* Fast enough to catch a button press, slow enough not to drown the
		 * log or the console in file writes. */
		since_diag += g_cfg.poll_ms;
		if (g_cfg.diag && since_diag >= DIAG_MS) {
			since_diag = 0;
			diag_watch();
		}

		since_rescan += g_cfg.poll_ms;
		if (since_rescan >= RESCAN_MS) {
			since_rescan = 0;
			if (g_cfg.diag)
				diag_flush();

			refresh_ports();
			dump_ports_once();
			if (usb_ok)
				usb_report_probes();

			/* Persist whatever the sticks have taught us. Rate limited
			 * inside, so this is a no-op most of the time. */
			if (g_cfg.stick_calibrate)
				sticks_save_if_dirty(0);

			/* In AUTO mode a cabled controller always wins: only look for
			 * Bluetooth pads when nothing is attached over USB and capacity remains. */
			if (g_cfg.mode == DS45_MODE_BT ||
			    (g_cfg.mode == DS45_MODE_AUTO && ds45_usb_pads_active() == 0 && ds45_bt_pads_active() < (int)g_cfg.max_pads))
				passthru_scan();
		}
	}

	if (g_cfg.stick_calibrate)
		sticks_save_if_dirty(1);

	vsh_hooks_remove();
	rumble_remove();
	passthru_release();
	if (usb_ok)
		usb_shutdown();
	diag_flush();

	sys_ppu_thread_exit(0);
#endif /* DS45_GAME */
}

/* ------------------------------------------------------------------------ */
/* PRX entry points                                                          */
/* ------------------------------------------------------------------------ */

static inline void thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

int ds45_start(uint64_t arg)
{
	int result;
	(void)arg;

	running = 1;
	result = sys_ppu_thread_create(&thread_id, ds45_thread, 0, THREAD_PRIO,
	                      THREAD_STACK, SYS_PPU_THREAD_CREATE_JOINABLE,
	                      THREAD_NAME);
	if (result != 0) {
		running = 0;
		return SYS_PRX_NO_RESIDENT;
	}

	thread_exit(0);
	return SYS_PRX_RESIDENT;
}

static void ds45_stop_thread(uint64_t arg)
{
	uint64_t exit_code;

	(void)arg;

	running = 0;
	sys_timer_usleep(300 * 1000);

	if (thread_id != (sys_ppu_thread_t)-1)
		sys_ppu_thread_join(thread_id, &exit_code);

#ifndef DS45_GAME
	/* Never in the game build. ds45_notify() resolves vshtask by walking the
	 * table at 0x1008C + 0x984, which only *is* an import table in the VSH
	 * process. In a game process that address holds arbitrary EBOOT data being
	 * read as import stubs: every pointer taken from it is range checked, but
	 * a plausible address is not necessarily a mapped one, so the walk can
	 * fault. A notification cannot reach the screen from a game anyway. */
	if (g_cfg.notify)
		ds45_notify("DS45Pad Disattivato / Unloaded");
#endif
	sys_ppu_thread_exit(0);
}

int ds45_stop(void)
{
	sys_ppu_thread_t t;
	uint64_t exit_code;

	sys_ppu_thread_create(&t, ds45_stop_thread, 0, THREAD_PRIO,
	                      THREAD_STACK, SYS_PPU_THREAD_CREATE_JOINABLE,
	                      STOP_THREAD_NAME);
	sys_ppu_thread_join(t, &exit_code);

	thread_exit(0);
	return SYS_PRX_STOP_OK;
}
