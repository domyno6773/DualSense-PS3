/*
 * DS45PAD - Full DualShock 4 / DualSense compatibility plugin for PlayStation 3
 *
 * Shared types, constants and module-wide declarations.
 *
 * The plugin claims a DS4/DualSense over USB (or reads the OEM Bluetooth pad in
 * passthrough mode), translates its HID reports into a DualShock 3 shaped
 * CellPadData and injects it through the libpad LDD (Logical Device Driver)
 * interface as a controller that advertises full PS3 conformity.
 *
 * Licensed under GPLv2 - see LICENSE.
 */
#ifndef DS45PAD_H
#define DS45PAD_H

#include <stdint.h>
#include <stddef.h>
#include "report_queue.h"

#define DS45PAD_VERSION "1.0.1-ps-usb-candidate"

/* Where the plugin looks for its data files. */
#define DS45_DIR          "/dev_hdd0/plugins/ds45pad"
#define DS45_CONFIG_FILE  DS45_DIR "/ds45pad.txt"
#define DS45_DEVICE_FILE  DS45_DIR "/ds45pad_devices.txt"
#define DS45_CAL_FILE     DS45_DIR "/ds45pad_cal.txt"
#define DS45_DIAG_FILE    "/dev_hdd0/tmp/ds45pad_diag.txt"
#define DS45_BOOT_FILE    "/dev_hdd0/tmp/ds45pad_boot.txt"

/* ------------------------------------------------------------------------ */
/* libpad constants                                                          */
/*                                                                           */
/* These come from <cell/pad.h> in the official SDK. They are re-declared    */
/* here behind #ifndef so the translation layer can be read (and unit        */
/* checked) without the proprietary headers, and so a missing/renamed SDK    */
/* symbol produces a clean value instead of a build break.                   */
/* Values verified against RPCS3's rpcs3/Emu/Io/pad_types.h.                 */
/* ------------------------------------------------------------------------ */

/* Deliberately not named CELL_PAD_MAX_PORT_NUM: the SDK may declare that as
 * an enumerator rather than a macro, and shadowing it with a #define would
 * depend on header include order to stay harmless. */
#define DS45_MAX_PORT_NUM       7

/* Indices into CellPadData.button[] */
#define DS45_BTN_LDD            0   /* LDD-only extension word          */
#define DS45_BTN_DIGITAL1       2
#define DS45_BTN_DIGITAL2       3
#define DS45_BTN_ANALOG_RIGHT_X 4
#define DS45_BTN_ANALOG_RIGHT_Y 5
#define DS45_BTN_ANALOG_LEFT_X  6
#define DS45_BTN_ANALOG_LEFT_Y  7
#define DS45_BTN_PRESS_RIGHT    8
#define DS45_BTN_PRESS_LEFT     9
#define DS45_BTN_PRESS_UP       10
#define DS45_BTN_PRESS_DOWN     11
#define DS45_BTN_PRESS_TRIANGLE 12
#define DS45_BTN_PRESS_CIRCLE   13
#define DS45_BTN_PRESS_CROSS    14
#define DS45_BTN_PRESS_SQUARE   15
#define DS45_BTN_PRESS_L1       16
#define DS45_BTN_PRESS_R1       17
#define DS45_BTN_PRESS_L2       18
#define DS45_BTN_PRESS_R2       19
#define DS45_BTN_SENSOR_X       20
#define DS45_BTN_SENSOR_Y       21
#define DS45_BTN_SENSOR_Z       22
#define DS45_BTN_SENSOR_G       23

/* A DS3 in press+sensor mode reports 24 codes. */
#define DS45_PAD_LEN            24

/* button[DS45_BTN_DIGITAL1] bits */
#define DS45_CTRL_SELECT        0x0001
#define DS45_CTRL_L3            0x0002
#define DS45_CTRL_R3            0x0004
#define DS45_CTRL_START         0x0008
#define DS45_CTRL_UP            0x0010
#define DS45_CTRL_RIGHT         0x0020
#define DS45_CTRL_DOWN          0x0040
#define DS45_CTRL_LEFT          0x0080
#define DS45_CTRL_PS            0x0100  /* PS button, digital1 extension */

/* button[DS45_BTN_DIGITAL2] bits */
#define DS45_CTRL_L2            0x0001
#define DS45_CTRL_R2            0x0002
#define DS45_CTRL_L1            0x0004
#define DS45_CTRL_R1            0x0008
#define DS45_CTRL_TRIANGLE      0x0010
#define DS45_CTRL_CIRCLE        0x0020
#define DS45_CTRL_CROSS         0x0040
#define DS45_CTRL_SQUARE        0x0080

/* button[DS45_BTN_LDD] bits - only meaningful for LDD pads. This is the one
 * that actually makes the XMB react to the PS button. */
#define DS45_CTRL_LDD_PS        0x0001

/* Device capability bits reported through cellPadGetInfo2(). Games that
 * "require an official DualShock 3" are almost always testing PS3_CONFORMITY. */
#define DS45_CAP_PS3_CONFORMITY 0x0001
#define DS45_CAP_PRESS_MODE     0x0002
#define DS45_CAP_SENSOR_MODE    0x0004
#define DS45_CAP_HP_ANALOG      0x0008
#define DS45_CAP_ACTUATOR       0x0010

#define DS45_SETTING_PRESS_ON   0x0002
#define DS45_SETTING_SENSOR_ON  0x0004

#define DS45_STATUS_CONNECTED       0x0001
#define DS45_STATUS_ASSIGN_CHANGES  0x0002

#define DS45_DEV_TYPE_STANDARD  0
#define DS45_DEV_TYPE_LDD       5

/* Neutral values for a DS3 at rest. */
#define DS45_STICK_CENTER       0x0080
#define DS45_SENSOR_CENTER      0x0200  /* 512, sensors are 10-bit */

/* ------------------------------------------------------------------------ */
/* Controller models                                                         */
/* ------------------------------------------------------------------------ */

#define DS45_VID_SONY           0x054c
#define DS45_PID_DS4_V1         0x05c4  /* CUH-ZCT1 */
#define DS45_PID_DS4_V2         0x09cc  /* CUH-ZCT2 */
#define DS45_PID_DS4_DONGLE     0x0ba0  /* DUALSHOCK 4 USB Wireless Adaptor */
#define DS45_PID_DUALSENSE      0x0ce6  /* CFI-ZCT1 */
#define DS45_PID_DUALSENSE_EDGE 0x0df2  /* CFI-ZCT1W */

typedef enum {
	DS45_MODEL_UNKNOWN = 0,
	DS45_MODEL_DS4,          /* DualShock 4, either revision + dongle */
	DS45_MODEL_DUALSENSE,    /* DualSense and DualSense Edge          */
} ds45_model_t;

typedef enum {
	DS45_LINK_USB = 0,
	DS45_LINK_BT,            /* reports carry the BT framing + CRC32 */
} ds45_link_t;

/* ------------------------------------------------------------------------ */
/* Normalised controller state                                               */
/*                                                                           */
/* Both HID parsers fill this in; a single translation stage turns it into   */
/* CellPadData. Keeping the parsers free of any libpad knowledge is what     */
/* lets the DS4 and DualSense paths share all downstream behaviour.          */
/* ------------------------------------------------------------------------ */

typedef struct {
	uint16_t x;      /* 0..1919 (DS4) / 0..1919 (DualSense) */
	uint16_t y;      /* 0..941  (DS4) / 0..1079 (DualSense) */
	uint8_t  id;
	uint8_t  active;
} ds45_touch_t;

typedef struct {
	/* Sticks: 0..255, 0x80 centred, Y grows downwards (same as a DS3). */
	uint8_t  lx, ly, rx, ry;

	/* Triggers: 0..255 analog travel. */
	uint8_t  l2, r2;

	/* Digital buttons, DS45_BTN_* independent bitfield. */
	uint32_t buttons;

	/* Motion, raw signed sensor units straight off the controller. */
	int16_t  gyro[3];   /* pitch, yaw, roll */
	int16_t  accel[3];  /* x, y, z          */

	/* Touchpad. */
	ds45_touch_t touch[2];

	/* Power: level 0..10, charging/full flags. */
	uint8_t  battery;
	uint8_t  charging;
	uint8_t  full;

	uint8_t  valid;         /* report parsed successfully              */
	uint8_t  sticks_valid;  /* the stick values came from the device,
	                         * not from an unpopulated report - the
	                         * calibration must never learn from those */
} ds45_state_t;

/* ds45_state_t.buttons bits (parser-neutral, not the libpad layout) */
#define DS45_B_CROSS      0x00000001
#define DS45_B_CIRCLE     0x00000002
#define DS45_B_SQUARE     0x00000004
#define DS45_B_TRIANGLE   0x00000008
#define DS45_B_L1         0x00000010
#define DS45_B_R1         0x00000020
#define DS45_B_L2         0x00000040
#define DS45_B_R2         0x00000080
#define DS45_B_SHARE      0x00000100  /* Share / Create        */
#define DS45_B_OPTIONS    0x00000200
#define DS45_B_L3         0x00000400
#define DS45_B_R3         0x00000800
#define DS45_B_PS         0x00001000
#define DS45_B_TOUCHPAD   0x00002000  /* touchpad click        */
#define DS45_B_MUTE       0x00004000  /* DualSense mic mute    */
#define DS45_B_UP         0x00008000
#define DS45_B_RIGHT      0x00010000
#define DS45_B_DOWN       0x00020000
#define DS45_B_LEFT       0x00040000
#define DS45_B_FN_L       0x00080000  /* DualSense Edge        */
#define DS45_B_FN_R       0x00100000
#define DS45_B_PADDLE_L   0x00200000
#define DS45_B_PADDLE_R   0x00400000

/* ------------------------------------------------------------------------ */
/* Output (rumble / lightbar / player LEDs)                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
	uint8_t rumble_weak;    /* right / high frequency motor */
	uint8_t rumble_strong;  /* left  / low  frequency motor */
	uint8_t led_r, led_g, led_b;
	uint8_t player;         /* 1..4, 0 = unassigned         */
	uint8_t dirty;
} ds45_output_t;

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

typedef enum {
	DS45_MODE_AUTO = 0,   /* USB when cabled, otherwise BT passthrough */
	DS45_MODE_USB,        /* USB only                                  */
	DS45_MODE_BT,         /* internal-Bluetooth passthrough only       */
} ds45_mode_t;

typedef enum {
	DS45_PRESSURE_DIGITAL = 0, /* held -> 255                            */
	DS45_PRESSURE_RAMP,        /* held -> ramps up over PRESSURE_RAMP_MS */
} ds45_pressure_t;

/* ------------------------------------------------------------------------ */
/* Analog stick calibration                                                  */
/*                                                                           */
/* One of these per axis, per controller. `min`/`max` are the extremes the    */
/* stick has actually been seen to reach and `ctr` its resting position; the  */
/* transform in sticks.c normalises against them before applying the DS3      */
/* saturation curve. Everything is learned at runtime, so a Hall effect       */
/* module that only swings 0x14..0xEB still ends up producing full travel.    */
/* ------------------------------------------------------------------------ */

#define DS45_CAL_BUCKETS 16   /* histogram resolution: 256 / 16 = 16 units */

typedef struct {
	uint8_t  min, max, ctr;
	uint8_t  rest_cand;      /* candidate resting value being confirmed   */
	uint16_t rest_hits;
	uint16_t centre_budget;  /* samples left in which ctr may still move  */
	uint8_t  hist[DS45_CAL_BUCKETS]; /* raw values seen this window       */
	uint16_t win_count;      /* samples into the current window           */
} ds45_axis_cal_t;

/* Axis order: LX, LY, RX, RY. */
#define DS45_AXIS_LX 0
#define DS45_AXIS_LY 1
#define DS45_AXIS_RX 2
#define DS45_AXIS_RY 3
#define DS45_AXIS_NUM 4

typedef struct {
	ds45_axis_cal_t axis[DS45_AXIS_NUM];
	int             dirty;
} ds45_stickcal_t;

typedef enum {
	DS45_TOUCH_OFF = 0,
	DS45_TOUCH_BUTTONS,   /* left/right halves act as two buttons */
	DS45_TOUCH_RSTICK,    /* finger position drives right stick   */
} ds45_touchmode_t;

/* Substitute for the PS button when the transport cannot deliver a real one
 * (Bluetooth passthrough). The combination is swallowed, so the games never
 * see the buttons it is built from. */
typedef enum {
	DS45_PSCOMBO_NONE = 0,
	DS45_PSCOMBO_SELECT_START,
	DS45_PSCOMBO_L3_R3,
} ds45_pscombo_t;

/* Symbolic targets a physical control can be remapped onto. */
typedef enum {
	DS45_MAP_NONE = 0,
	DS45_MAP_CROSS, DS45_MAP_CIRCLE, DS45_MAP_SQUARE, DS45_MAP_TRIANGLE,
	DS45_MAP_L1, DS45_MAP_R1, DS45_MAP_L2, DS45_MAP_R2,
	DS45_MAP_L3, DS45_MAP_R3,
	DS45_MAP_START, DS45_MAP_SELECT, DS45_MAP_PS,
	DS45_MAP_UP, DS45_MAP_RIGHT, DS45_MAP_DOWN, DS45_MAP_LEFT,
	DS45_MAP_MAX
} ds45_map_t;

typedef struct {
	/* Master switch. With this off the plugin loads, writes its boot trace
	 * and then does nothing at all - the baseline for telling "the module is
	 * harmless" apart from "something the module does breaks the console". */
	int              enable;
	/* Whether injected samples are made visible to games as well as the XMB.
	 * On by default; turning it off is the way to find out whether that is
	 * what stalls an app while it loads. */
	int              insert_into_game;
	int              allow_insert_combo; /* legacy service shortcut, opt-in */

	ds45_mode_t      mode;
	ds45_pressure_t  pressure;
	uint32_t         pressure_ramp_ms;
	ds45_touchmode_t touch_mode;

	uint32_t poll_ms;          /* LDD injection period                    */
	/* Hard limit on virtual pads. One by default: a single controller is the
	 * normal case, and the cap is what stops a runaway from filling every
	 * port on the console. */
	uint32_t max_pads;

	/* Sticks. `saturation` is the headline control: the percentage of
	 * measured travel that counts as full deflection. A DualShock 3's ADC
	 * saturates at roughly 80% of its mechanical range, which is why its
	 * corners read far closer to 0x00/0xFF than a linear DS4/DualSense.
	 *   100 = raw, no correction
	 *    80 = DualShock 3 equivalent (default)
	 *    71 = diagonals reach the corners exactly */
	uint32_t stick_saturation; /* 50..100                                 */
	uint32_t stick_deadzone;   /* radial, percent of travel               */
	int      stick_calibrate;  /* learn each stick's real range at runtime */

	int      sixaxis;          /* master enable for motion forwarding     */
	int      accel_scale;      /* percent, 100 = physically correct       */
	int      gyro_scale;       /* percent                                 */
	int      invert_x, invert_y, invert_z, invert_g;
	int      swap_xy;

	int      rumble;
	int      lightbar;         /* colour the lightbar per player          */
	int      dim_lightbar;     /* keep it dim to save battery             */

	/* Capability bits advertised to games through cellPadGetInfo2(). The
	 * five documented bits are the default; PS3xPAD historically passed
	 * 0xFFFF, which is worth trying if a game still refuses the pad. */
	uint32_t capability_mask;
	/* How far the mask is shifted before going into syscall 574. Every
	 * published plugin uses 1; measured behaviour suggests the system
	 * grants only a subset, so this is exposed for experimentation. */
	uint32_t capability_shift;
	/* Device type reported for the virtual pad. 5 = LDD, which is what the
	 * syscall is normally given; a real DualShock 3 reports 0 (STANDARD),
	 * and a game that gates on the type rather than on the conformity bit
	 * will only accept 0. */
	uint32_t ldd_dev_type;

	ds45_map_t map_touchpad;   /* touchpad click target                   */
	ds45_map_t map_mute;       /* DualSense mute button target            */
	ds45_map_t map_touch_left; /* touch zones when touch_mode = BUTTONS   */
	ds45_map_t map_touch_right;
	ds45_map_t map_fn_l, map_fn_r, map_paddle_l, map_paddle_r;

	/* Passthrough mode: which OEM port to read, -1 = auto-detect. */
	int      bt_source_port;
	/* Where the PS button lands inside the OEM pad report, if anywhere:
	 * an index into CellPadData.button[] plus a bit mask, or -1 for "not
	 * reported". Discovered with the diagnostic dump - docs/DIAGNOSTICO.md. */
	int      bt_ps_code;
	int      bt_ps_mask;
	ds45_pscombo_t bt_ps_combo;

	/* In-game build only: answer cellPadGetInfo2 as a conformant DualShock 3
	 * so a game that gates on device type accepts whatever pad is present. */
	int      spoof_pad_info;
	/* Only for passthrough: raw USB ownership needs the full controller. */
	int      ps_only;

	int      diag;             /* write DS45_DIAG_FILE                    */
	int      notify;           /* show XMB notifications                  */
} ds45_config_t;

extern ds45_config_t g_cfg;

void ds45_config_defaults(ds45_config_t *c);
int  ds45_config_load(ds45_config_t *c, const char *path);

/* ------------------------------------------------------------------------ */
/* Per-controller runtime slot                                               */
/* ------------------------------------------------------------------------ */

#define DS45_MAX_PADS 4

typedef struct ds45_pad {
	int           in_use;
	ds45_model_t  model;
	ds45_link_t   link;

	/* USB plumbing (unused in passthrough mode). */
	int32_t       dev_id;
	int32_t       c_pipe, i_pipe, o_pipe;
	int32_t       in_size, out_size;

	/* Passthrough plumbing. */
	int           src_port;

	/* USB callbacks copy completed transfers into reports under usb_mutex.
	 * The polling thread parses private copies in arrival order. */
	ds45_state_t  state;
	int           buf_idx;

	/* libpad LDD handle and the port the system gave us. */
	int32_t       handle;
	int32_t       port;
	int           insert_on;   /* is the pad currently visible to games? */
	int           combo_held;   /* debounced state of the toggle combo   */
	int           combo_stable; /* samples the new state has held        */
	/* Set by the USB attach callback, acted on by the polling thread.
	 * Registering the virtual pad involves a 10 ms sleep and several file
	 * writes, and doing that inside a USB enumeration callback blocks the
	 * USB manager thread - which is exactly what apps wait on while they
	 * start up. The callback must set up the slot and return immediately. */
	volatile int  needs_register;
	int           read_fails;   /* consecutive source-read failures */
	int           read_blocked; /* reads failing while the port is present */
	int           inject_error;
	int           ps_sent;
	int           ps_seeded;
	ds45_report_queue_t reports;
	int           usb_ps_suppressed;
	int           usb_report_seen;
	int           usb_error;
	int           usb_error_reported;

	ds45_output_t   out;
	ds45_stickcal_t cal;
	uint32_t      press_ticks[16];   /* ramp accumulators, per press code */
	uint32_t      out_seq;           /* DualSense Bluetooth sequence tag  */

	/* USB transfer buffers. These must outlive each transfer, so they live
	 * in the slot rather than on a stack. */
	uint8_t       buf[2][80];
	uint8_t       obuf[80];
} ds45_pad_t;

extern ds45_pad_t g_pads[DS45_MAX_PADS];

/* ------------------------------------------------------------------------ */
/* Module interfaces                                                         */
/* ------------------------------------------------------------------------ */

/* ds4.c / dualsense.c - HID report translation */
int  ds4_parse(const uint8_t *buf, int len, ds45_link_t link, ds45_state_t *st);
int  ds4_build_output(uint8_t *buf, int cap, ds45_link_t link,
                      const ds45_output_t *out);
int  dualsense_parse(const uint8_t *buf, int len, ds45_link_t link,
                     ds45_state_t *st);
int  dualsense_build_output(uint8_t *buf, int cap, ds45_link_t link,
                            const ds45_output_t *out);

/* sticks.c - calibration and the DualShock 3 stick response curve */
void sticks_cal_defaults(ds45_stickcal_t *cal);
void sticks_load(void);
void sticks_save_if_dirty(int force);
void sticks_transform(ds45_pad_t *pad, const ds45_state_t *st,
                      uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry);

/* padldd.c - virtual controller */
int  ldd_register(ds45_pad_t *pad);
int  ldd_set_insert_into_game(ds45_pad_t *pad, int on);
void ldd_check_combo(ds45_pad_t *pad);
void ldd_unregister(ds45_pad_t *pad);
void ldd_inject(ds45_pad_t *pad);
void ldd_translate(ds45_pad_t *pad, const ds45_state_t *st, uint16_t *button);

/* usb.c - cellUsbd device driver */
int  usb_init(void);
void usb_poll(void);
void usb_report_probes(void);
void usb_shutdown(void);
void usb_send_output(ds45_pad_t *pad);

/* passthru.c - internal Bluetooth passthrough */
int  passthru_scan(void);
void passthru_poll(void);
void passthru_release(void);

/* gamehook.c - in-game stick correction (DS45_GAME build) */
int  gamehook_install(void);
void gamehook_remove(void);

/* diag.c */
void diag_dump_ports(void);
void diag_watch(void);
void diag_state(const ds45_pad_t *pad);
void diag_sticks(uint8_t in_x, uint8_t in_y, uint8_t out_x, uint8_t out_y,
                 const ds45_axis_cal_t *cx, const ds45_axis_cal_t *cy);
void diag_msg(const char *msg);
void diag_observe(int port, const void *data);
void diag_flush(void);

/* util.c */
void  ds45_notify(const char *msg);
void  ds45_boot_mark(const char *stage);
void  ds45_boot_mark_hex(const char *stage, uint32_t value);
uint32_t ds45_crc32(uint32_t crc, const uint8_t *data, int len);
int   ds45_read_file(const char *path, char *out, int cap);
ds45_map_t ds45_parse_map(const char *name);
void  ds45_apply_map(ds45_map_t m, uint16_t *button);

/* Number of controllers currently producing input. */
int   ds45_active_pads(void);

#endif /* DS45PAD_H */
