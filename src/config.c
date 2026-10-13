/*
 * DS45PAD - configuration file.
 *
 * Plain "KEY = VALUE" lines, '#' starts a comment. Everything has a working
 * default, so a missing or partial file is not an error - the plugin simply
 * runs with the defaults.
 */

#include <string.h>

#include "ds45pad.h"

ds45_config_t g_cfg;

void ds45_config_defaults(ds45_config_t *c)
{
	memset(c, 0, sizeof(*c));

	c->enable = 1;
	c->insert_into_game = 1;
	c->allow_insert_combo = 1;

	/* AUTO mode: raw USB when cabled, otherwise Bluetooth passthrough */
	c->mode             = DS45_MODE_AUTO;
	c->pressure         = DS45_PRESSURE_DIGITAL;
	c->pressure_ramp_ms = 200;
	c->touch_mode       = DS45_TOUCH_OFF;

	/* 10 ms, the value the reference plugin uses. 4 ms tracked the
	 * controller's own report rate but cost noticeably more system time. */
	c->poll_ms  = 10;
	c->max_pads = 1;

	c->stick_saturation = 80;  /* DualShock 3 equivalent - see sticks.c */
	c->stick_deadzone   = 4;
	c->stick_calibrate  = 1;

	c->sixaxis     = 0;   /* off: a still DualShock 3 is what games then see */
	c->accel_scale = 100; /* 100% = physically equivalent to a DualShock 3 */
	c->gyro_scale  = 100;

	c->rumble       = 0;
	c->lightbar     = 1;
	c->dim_lightbar = 1;

	c->capability_mask = DS45_CAP_PS3_CONFORMITY | DS45_CAP_PRESS_MODE |
	                     DS45_CAP_SENSOR_MODE | DS45_CAP_HP_ANALOG |
	                     DS45_CAP_ACTUATOR;
	c->capability_shift = 1;
	c->ldd_dev_type     = DS45_DEV_TYPE_LDD;

	c->map_touchpad    = DS45_MAP_NONE;
	c->map_mute        = DS45_MAP_NONE;
	c->map_touch_left  = DS45_MAP_NONE;
	c->map_touch_right = DS45_MAP_NONE;
	c->map_fn_l        = DS45_MAP_NONE;
	c->map_fn_r        = DS45_MAP_NONE;
	c->map_paddle_l    = DS45_MAP_L1;
	c->map_paddle_r    = DS45_MAP_R1;

	c->bt_source_port = -1;   /* auto-detect */
	c->bt_ps_code     = -1;   /* unknown until measured */
	c->bt_ps_mask     = 0;
	c->bt_ps_combo    = DS45_PSCOMBO_SELECT_START;

	c->spoof_pad_info = 1;
	c->ps_only = 1;

	c->diag   = 0;
	c->notify = 0;   /* the boot trace is the safe way to confirm loading */
}

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int parse_int(const char *p, int *out)
{
	int neg = 0, seen = 0;
	long v = 0;

	while (is_space(*p)) p++;

	if (*p == '-') { neg = 1; p++; }
	else if (*p == '+') p++;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		for (;;) {
			int d;

			if (*p >= '0' && *p <= '9')      d = *p - '0';
			else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
			else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
			else break;

			v = v * 16 + d;
			p++;
			seen = 1;
		}
	} else {
		while (*p >= '0' && *p <= '9') {
			v = v * 10 + (*p - '0');
			p++;
			seen = 1;
		}
	}

	if (!seen)
		return 0;

	*out = (int)(neg ? -v : v);
	return 1;
}

static int match(const char *val, const char *word)
{
	return strcmp(val, word) == 0;
}

static void apply(ds45_config_t *c, const char *key, const char *val)
{
	int n;

	if (match(key, "ENABLE")) {
		if (parse_int(val, &n)) c->enable = n;
	} else if (match(key, "INSERT_INTO_GAME")) {
		if (parse_int(val, &n)) c->insert_into_game = n;
	} else if (match(key, "ALLOW_INSERT_COMBO")) {
		if (parse_int(val, &n)) c->allow_insert_combo = n != 0;
	} else if (match(key, "MODE")) {
		if      (match(val, "USB"))  c->mode = DS45_MODE_USB;
		else if (match(val, "BT"))   c->mode = DS45_MODE_BT;
		else                         c->mode = DS45_MODE_AUTO;
	} else if (match(key, "POLL_MS")) {
		if (parse_int(val, &n) && n >= 1 && n <= 50) c->poll_ms = (uint32_t)n;
	} else if (match(key, "MAX_PADS")) {
		if (parse_int(val, &n) && n >= 1 && n <= DS45_MAX_PADS)
			c->max_pads = (uint32_t)n;
	} else if (match(key, "STICK_SATURATION")) {
		/* Below 50 the clamp would eat most of the usable travel. */
		if (parse_int(val, &n) && n >= 50 && n <= 100)
			c->stick_saturation = (uint32_t)n;
	} else if (match(key, "STICK_DEADZONE")) {
		if (parse_int(val, &n) && n >= 0 && n <= 50)
			c->stick_deadzone = (uint32_t)n;
	} else if (match(key, "STICK_CALIBRATION")) {
		c->stick_calibrate = match(val, "OFF") ? 0 : 1;
	} else if (match(key, "PRESSURE")) {
		c->pressure = match(val, "RAMP") ? DS45_PRESSURE_RAMP
		                                 : DS45_PRESSURE_DIGITAL;
	} else if (match(key, "PRESSURE_RAMP_MS")) {
		if (parse_int(val, &n) && n > 0) c->pressure_ramp_ms = (uint32_t)n;
	} else if (match(key, "TOUCHPAD_MODE")) {
		if      (match(val, "BUTTONS")) c->touch_mode = DS45_TOUCH_BUTTONS;
		else if (match(val, "RSTICK"))  c->touch_mode = DS45_TOUCH_RSTICK;
		else                            c->touch_mode = DS45_TOUCH_OFF;
	} else if (match(key, "SIXAXIS")) {
		if (parse_int(val, &n)) c->sixaxis = n;
	} else if (match(key, "ACCEL_SCALE")) {
		if (parse_int(val, &n) && n > 0 && n <= 1000) c->accel_scale = n;
	} else if (match(key, "GYRO_SCALE")) {
		if (parse_int(val, &n) && n > 0 && n <= 1000) c->gyro_scale = n;
	} else if (match(key, "INVERT_X")) {
		if (parse_int(val, &n)) c->invert_x = n;
	} else if (match(key, "INVERT_Y")) {
		if (parse_int(val, &n)) c->invert_y = n;
	} else if (match(key, "INVERT_Z")) {
		if (parse_int(val, &n)) c->invert_z = n;
	} else if (match(key, "INVERT_G")) {
		if (parse_int(val, &n)) c->invert_g = n;
	} else if (match(key, "SWAP_XY")) {
		if (parse_int(val, &n)) c->swap_xy = n;
	} else if (match(key, "RUMBLE")) {
		if (parse_int(val, &n)) c->rumble = n;
	} else if (match(key, "LIGHTBAR")) {
		if (parse_int(val, &n)) c->lightbar = n;
	} else if (match(key, "DIM_LIGHTBAR")) {
		if (parse_int(val, &n)) c->dim_lightbar = n;
	} else if (match(key, "CAPABILITY_MASK")) {
		if (parse_int(val, &n)) c->capability_mask = (uint32_t)n;
	} else if (match(key, "CAPABILITY_SHIFT")) {
		if (parse_int(val, &n) && n >= 0 && n <= 8)
			c->capability_shift = (uint32_t)n;
	} else if (match(key, "LDD_DEV_TYPE")) {
		if (parse_int(val, &n) && n >= 0 && n <= 7)
			c->ldd_dev_type = (uint32_t)n;
	} else if (match(key, "MAP_TOUCHPAD")) {
		c->map_touchpad = ds45_parse_map(val);
	} else if (match(key, "MAP_MUTE")) {
		c->map_mute = ds45_parse_map(val);
	} else if (match(key, "MAP_TOUCH_LEFT")) {
		c->map_touch_left = ds45_parse_map(val);
	} else if (match(key, "MAP_TOUCH_RIGHT")) {
		c->map_touch_right = ds45_parse_map(val);
	} else if (match(key, "MAP_FN_L")) {
		c->map_fn_l = ds45_parse_map(val);
	} else if (match(key, "MAP_FN_R")) {
		c->map_fn_r = ds45_parse_map(val);
	} else if (match(key, "MAP_PADDLE_L")) {
		c->map_paddle_l = ds45_parse_map(val);
	} else if (match(key, "MAP_PADDLE_R")) {
		c->map_paddle_r = ds45_parse_map(val);
	} else if (match(key, "BT_SOURCE_PORT")) {
		if (parse_int(val, &n) && n >= -1 && n < DS45_MAX_PORT_NUM)
			c->bt_source_port = n;
	} else if (match(key, "BT_PS_CODE")) {
		if (parse_int(val, &n) && n >= -1 && n < 64) c->bt_ps_code = n;
	} else if (match(key, "BT_PS_MASK")) {
		if (parse_int(val, &n) && n >= 0 && n <= 0xFFFF) c->bt_ps_mask = n;
	} else if (match(key, "BT_PS_COMBO")) {
		if      (match(val, "SELECT_START")) c->bt_ps_combo = DS45_PSCOMBO_SELECT_START;
		else if (match(val, "L3_R3"))        c->bt_ps_combo = DS45_PSCOMBO_L3_R3;
		else                                 c->bt_ps_combo = DS45_PSCOMBO_NONE;
	} else if (match(key, "SPOOF_PAD_INFO")) {
		if (parse_int(val, &n)) c->spoof_pad_info = n;
	} else if (match(key, "PS_ONLY")) {
		if (parse_int(val, &n)) c->ps_only = n != 0;
	} else if (match(key, "DIAG")) {
		if (parse_int(val, &n)) c->diag = n;
	} else if (match(key, "NOTIFY")) {
		if (parse_int(val, &n)) c->notify = n;
	}
}

int ds45_config_load(ds45_config_t *c, const char *path)
{
	/* Generous on purpose. An earlier 4 KB buffer silently swallowed
	 * everything past byte 4095 of a config that had grown to 8 KB, so 34 of
	 * 39 settings were being ignored with no indication whatsoever - MODE
	 * among them. Truncation is now both unlikely and reported. */
	static char text[16384];
	char key[40], val[40];
	char *line;
	int len, i;

	len = ds45_read_file(path, text, sizeof(text) - 1);
	if (len <= 0) {
		ds45_boot_mark("config_NOT_FOUND_using_defaults");
		return -1;
	}
	text[len] = 0;

	ds45_boot_mark_hex("config_bytes", (uint32_t)len);
	if (len >= (int)sizeof(text) - 1)
		ds45_boot_mark("config_TRUNCATED_settings_lost");

	line = text;
	while (*line) {
		char *next = line;
		char *eq;

		while (*next && *next != '\n') next++;
		if (*next == '\n') *next++ = 0;

		while (is_space(*line)) line++;

		if (*line == '#' || *line == 0) {
			line = next;
			continue;
		}

		eq = strchr(line, '=');
		if (!eq) {
			line = next;
			continue;
		}

		/* key: trim, upper-case */
		{
			char *k = line;
			char *kend = eq;

			while (kend > k && is_space(kend[-1])) kend--;
			for (i = 0; i < (int)sizeof(key) - 1 && k + i < kend; i++)
				key[i] = upper(k[i]);
			key[i] = 0;
		}

		/* value: trim, strip trailing comment, upper-case */
		{
			char *v = eq + 1;
			char *vend;

			while (is_space(*v)) v++;
			vend = v;
			while (*vend && *vend != '#') vend++;
			while (vend > v && is_space(vend[-1])) vend--;

			for (i = 0; i < (int)sizeof(val) - 1 && v + i < vend; i++)
				val[i] = upper(v[i]);
			val[i] = 0;
		}

		apply(c, key, val);
		line = next;
	}

	/* Echo back the settings that decide which code path runs at all, so the
	 * boot trace shows what the plugin actually believes rather than what the
	 * file says. */
	ds45_boot_mark_hex("cfg_mode", (uint32_t)c->mode);
	ds45_boot_mark_hex("cfg_insert_into_game", (uint32_t)c->insert_into_game);
	ds45_boot_mark_hex("cfg_max_pads", c->max_pads);
	ds45_boot_mark_hex("cfg_bt_source_port", (uint32_t)c->bt_source_port);
	ds45_boot_mark_hex("cfg_ps_only", (uint32_t)c->ps_only);
	ds45_boot_mark_hex("cfg_bt_ps_combo", (uint32_t)c->bt_ps_combo);

	return 0;
}
