/*
 * DS45PAD - DualSense / DualSense Edge HID report translation.
 *
 *   USB input  : report 0x01, 64 bytes, payload starts at offset 1
 *   BT  input  : report 0x31, 78 bytes, payload starts at offset 2,
 *                trailing 4 bytes are a CRC32
 *   USB output : report 0x02, 63 bytes
 *   BT  output : report 0x31, 78 bytes, payload at offset 3, trailing CRC32
 *
 * Note the field order differs from the DualShock 4: the DualSense puts the
 * analog triggers *before* the button bytes, so the two parsers cannot share
 * a common payload layout.
 */

#include "ds45pad.h"

#define DS_IN_USB           0x01
#define DS_IN_BT            0x31

#define DS_OUT_USB          0x02
#define DS_OUT_BT           0x31

#define DS_OUT_USB_SIZE     63
#define DS_OUT_BT_SIZE      78
#define DS_OUT_TAG          0x10

#define DS_CRC_SEED_OUT     0xA2

#define DS_OFF_USB          1
#define DS_OFF_BT           2

/* Offsets inside the 63 byte common payload. */
#define DS_C_LX             0x00
#define DS_C_LY             0x01
#define DS_C_RX             0x02
#define DS_C_RY             0x03
#define DS_C_L2             0x04
#define DS_C_R2             0x05
#define DS_C_SEQ            0x06
#define DS_C_BUTTONS        0x07  /* 4 bytes      */
#define DS_C_GYRO           0x0F  /* 3 x int16 LE */
#define DS_C_ACCEL          0x15  /* 3 x int16 LE */
#define DS_C_TOUCH          0x20  /* 2 x 4 bytes  */
#define DS_C_STATUS         0x34  /* 3 bytes      */
#define DS_C_SIZE           0x3F

#define DS_STATUS0_BATTERY  0x0F
#define DS_STATUS0_CHARGING 0xF0
#define DS_CHARGING_ACTIVE  0x10
#define DS_CHARGING_FULL    0x20

/* Output report field offsets, relative to the start of the common block. */
#define DS_O_VALID0         0x00
#define DS_O_VALID1         0x01
#define DS_O_MOTOR_RIGHT    0x02
#define DS_O_MOTOR_LEFT     0x03
#define DS_O_VALID2         0x26
#define DS_O_LIGHTBAR_SETUP 0x29
#define DS_O_LED_BRIGHTNESS 0x2A
#define DS_O_PLAYER_LEDS    0x2B
#define DS_O_LIGHTBAR_R     0x2C
#define DS_O_LIGHTBAR_G     0x2D
#define DS_O_LIGHTBAR_B     0x2E

#define DS_VALID0_COMPAT_VIBRATION   0x01
#define DS_VALID0_HAPTICS_SELECT     0x02
#define DS_VALID1_LIGHTBAR_CONTROL   0x04
#define DS_VALID1_PLAYER_INDICATOR   0x10
#define DS_VALID2_LIGHTBAR_SETUP     0x02
#define DS_VALID2_COMPAT_VIBRATION2  0x04
#define DS_LIGHTBAR_SETUP_LIGHT_OUT  0x02

static int16_t rd16le(const uint8_t *p)
{
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void decode_hat(uint8_t hat, uint32_t *buttons)
{
	static const uint32_t dirs[8] = {
		DS45_B_UP,
		DS45_B_UP | DS45_B_RIGHT,
		DS45_B_RIGHT,
		DS45_B_RIGHT | DS45_B_DOWN,
		DS45_B_DOWN,
		DS45_B_DOWN | DS45_B_LEFT,
		DS45_B_LEFT,
		DS45_B_LEFT | DS45_B_UP,
	};

	if (hat < 8)
		*buttons |= dirs[hat];
}

static void decode_touch(const uint8_t *p, ds45_touch_t *t)
{
	t->active = (p[0] & 0x80) ? 0 : 1;
	t->id     = p[0] & 0x7F;
	t->x      = (uint16_t)p[1] | (((uint16_t)p[2] & 0x0F) << 8);
	t->y      = (uint16_t)((p[2] & 0xF0) >> 4) | ((uint16_t)p[3] << 4);
}

int dualsense_parse(const uint8_t *buf, int len, ds45_link_t link,
                    ds45_state_t *st)
{
	const uint8_t *c;
	int off, i;
	uint8_t b0, b1, b2, b3, status0, charge;

	if (!st)
		return -1;
	st->valid = 0;
	if (!buf || len <= 0)
		return -1;

	if (link == DS45_LINK_BT) {
		/* The DualSense only emits the full 0x31 report once the host has
		 * read its calibration feature report. The abbreviated 0x01 report
		 * it sends before that is not publicly documented, so we ignore it
		 * rather than guess at a layout. */
		if (buf[0] != DS_IN_BT || len != 78)
			return -1;
		{
			uint8_t seed = 0xA1; /* INPUT reports; output uses 0xA2. */
			uint32_t crc = ds45_crc32(0xFFFFFFFFu, &seed, 1);
			uint32_t expected = (uint32_t)buf[74] | ((uint32_t)buf[75] << 8) |
			                    ((uint32_t)buf[76] << 16) | ((uint32_t)buf[77] << 24);
			if (~ds45_crc32(crc, buf, 74) != expected)
				return -1;
		}
		off = DS_OFF_BT;
	} else {
		if (link != DS45_LINK_USB || buf[0] != DS_IN_USB || len != 64)
			return -1;
		off = DS_OFF_USB;
	}

	c = buf + off;

	st->lx = c[DS_C_LX];
	st->ly = c[DS_C_LY];
	st->rx = c[DS_C_RX];
	st->ry = c[DS_C_RY];
	st->l2 = c[DS_C_L2];
	st->r2 = c[DS_C_R2];

	b0 = c[DS_C_BUTTONS + 0];
	b1 = c[DS_C_BUTTONS + 1];
	b2 = c[DS_C_BUTTONS + 2];
	b3 = c[DS_C_BUTTONS + 3];
	(void)b3;

	st->buttons = 0;
	decode_hat(b0 & 0x0F, &st->buttons);
	if (b0 & 0x10) st->buttons |= DS45_B_SQUARE;
	if (b0 & 0x20) st->buttons |= DS45_B_CROSS;
	if (b0 & 0x40) st->buttons |= DS45_B_CIRCLE;
	if (b0 & 0x80) st->buttons |= DS45_B_TRIANGLE;

	if (b1 & 0x01) st->buttons |= DS45_B_L1;
	if (b1 & 0x02) st->buttons |= DS45_B_R1;
	if (b1 & 0x04) st->buttons |= DS45_B_L2;
	if (b1 & 0x08) st->buttons |= DS45_B_R2;
	if (b1 & 0x10) st->buttons |= DS45_B_SHARE;    /* Create */
	if (b1 & 0x20) st->buttons |= DS45_B_OPTIONS;
	if (b1 & 0x40) st->buttons |= DS45_B_L3;
	if (b1 & 0x80) st->buttons |= DS45_B_R3;

	if (b2 & 0x01) st->buttons |= DS45_B_PS;
	if (b2 & 0x02) st->buttons |= DS45_B_TOUCHPAD;
	if (b2 & 0x04) st->buttons |= DS45_B_MUTE;

	/* DualSense Edge extras. Mainline Linux does not decode these; the bit
	 * positions below are the community documented layout and are worth
	 * re-checking with the diagnostic dump on an actual Edge. */
	if (b2 & 0x10) st->buttons |= DS45_B_FN_L;
	if (b2 & 0x20) st->buttons |= DS45_B_FN_R;
	if (b2 & 0x40) st->buttons |= DS45_B_PADDLE_L;
	if (b2 & 0x80) st->buttons |= DS45_B_PADDLE_R;

	for (i = 0; i < 3; i++) {
		st->gyro[i]  = rd16le(c + DS_C_GYRO  + i * 2);
		st->accel[i] = rd16le(c + DS_C_ACCEL + i * 2);
	}

	decode_touch(c + DS_C_TOUCH + 0, &st->touch[0]);
	decode_touch(c + DS_C_TOUCH + 4, &st->touch[1]);

	status0      = c[DS_C_STATUS];
	st->battery  = status0 & DS_STATUS0_BATTERY;
	charge       = status0 & DS_STATUS0_CHARGING;
	st->charging = (charge == DS_CHARGING_ACTIVE);
	st->full     = (charge == DS_CHARGING_FULL);
	if (st->battery > 10)
		st->battery = 10;

	st->valid = 1;
	st->sticks_valid = 1;
	return 0;
}

int dualsense_build_output(uint8_t *buf, int cap, ds45_link_t link,
                           const ds45_output_t *out)
{
	/* Player indicator patterns for the five LEDs under the touchpad. */
	static const uint8_t player_led[4] = { 0x04, 0x0A, 0x15, 0x1B };
	int size, o;

	size = (link == DS45_LINK_BT) ? DS_OUT_BT_SIZE : DS_OUT_USB_SIZE;
	if (cap < size)
		return -1;

	for (o = 0; o < size; o++)
		buf[o] = 0;

	if (link == DS45_LINK_BT) {
		buf[0] = DS_OUT_BT;
		buf[1] = 0x00;          /* sequence tag, incremented by the caller */
		buf[2] = DS_OUT_TAG;
		o = 3;
	} else {
		buf[0] = DS_OUT_USB;
		o = 1;
	}

	/* COMPATIBLE_VIBRATION drives the classic rumble motors. Newer DualSense
	 * firmware also wants VIBRATION2 in valid_flag2 or the motors stay
	 * silent, so both are set. */
	buf[o + DS_O_VALID0] = DS_VALID0_COMPAT_VIBRATION | DS_VALID0_HAPTICS_SELECT;
	buf[o + DS_O_VALID1] = DS_VALID1_LIGHTBAR_CONTROL | DS_VALID1_PLAYER_INDICATOR;
	buf[o + DS_O_VALID2] = DS_VALID2_COMPAT_VIBRATION2 | DS_VALID2_LIGHTBAR_SETUP;

	buf[o + DS_O_MOTOR_RIGHT]    = out->rumble_weak;
	buf[o + DS_O_MOTOR_LEFT]     = out->rumble_strong;

	/* Skip the power-on lightbar fade so the colour we pick shows up at once. */
	buf[o + DS_O_LIGHTBAR_SETUP] = DS_LIGHTBAR_SETUP_LIGHT_OUT;
	buf[o + DS_O_LED_BRIGHTNESS] = 0x00;
	buf[o + DS_O_PLAYER_LEDS]    = (out->player >= 1 && out->player <= 4)
	                                ? player_led[out->player - 1] : 0x00;
	buf[o + DS_O_LIGHTBAR_R]     = out->led_r;
	buf[o + DS_O_LIGHTBAR_G]     = out->led_g;
	buf[o + DS_O_LIGHTBAR_B]     = out->led_b;

	if (link == DS45_LINK_BT) {
		uint8_t seed = DS_CRC_SEED_OUT;
		uint32_t crc = ds45_crc32(0xFFFFFFFF, &seed, 1);
		crc = ~ds45_crc32(crc, buf, size - 4);
		buf[size - 4] = (uint8_t)(crc & 0xFF);
		buf[size - 3] = (uint8_t)((crc >> 8) & 0xFF);
		buf[size - 2] = (uint8_t)((crc >> 16) & 0xFF);
		buf[size - 1] = (uint8_t)((crc >> 24) & 0xFF);
	}

	return size;
}
