/*
 * DS45PAD - DualShock 4 HID report translation.
 *
 * Report layouts follow the Linux hid-playstation driver, which is the best
 * documented public reference for these devices.
 *
 *   USB input   : report 0x01, 64 bytes, payload starts at offset 1
 *   BT  input   : report 0x11, 78 bytes, payload starts at offset 3,
 *                 trailing 4 bytes are a CRC32
 *   USB output  : report 0x05, 32 bytes
 *   BT  output  : report 0x11, 78 bytes, trailing CRC32 seeded with 0xA2
 *
 * A DS4 in Bluetooth mode only emits the 10 byte 0x01 report until the host
 * reads feature report 0x02; we handle both so a controller that has not been
 * "woken up" still produces usable input.
 */

#include "ds45pad.h"

#define DS4_IN_USB          0x01
#define DS4_IN_BT           0x11
#define DS4_IN_BT_MINIMAL   0x01

#define DS4_OUT_USB         0x05
#define DS4_OUT_BT          0x11

#define DS4_OUT_USB_SIZE    32
#define DS4_OUT_BT_SIZE     78

#define DS4_CRC_SEED_OUT    0xA2

/* Payload offset after the report id (and BT framing bytes). */
#define DS4_OFF_USB         1
#define DS4_OFF_BT          3

/* Offsets inside the 32 byte common payload. */
#define DS4_C_LX            0
#define DS4_C_LY            1
#define DS4_C_RX            2
#define DS4_C_RY            3
#define DS4_C_BUTTONS       4   /* 3 bytes */
#define DS4_C_L2            7
#define DS4_C_R2            8
#define DS4_C_GYRO          12  /* 3 x int16 LE */
#define DS4_C_ACCEL         18  /* 3 x int16 LE */
#define DS4_C_STATUS        29  /* 2 bytes      */
#define DS4_C_SIZE          32

#define DS4_STATUS0_BATTERY 0x0F
#define DS4_STATUS0_CABLE   0x10

static int16_t rd16le(const uint8_t *p)
{
	/* The PS3 is big endian, so the little endian sensor words have to be
	 * assembled by hand rather than cast. */
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Hat switch: 0 = north, going clockwise, 8 = released. */
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
	/* Bit 7 of the contact byte is set while the finger is *not* down. */
	t->active = (p[0] & 0x80) ? 0 : 1;
	t->id     = p[0] & 0x7F;
	t->x      = (uint16_t)p[1] | (((uint16_t)p[2] & 0x0F) << 8);
	t->y      = (uint16_t)((p[2] & 0xF0) >> 4) | ((uint16_t)p[3] << 4);
}

int ds4_parse(const uint8_t *buf, int len, ds45_link_t link, ds45_state_t *st)
{
	const uint8_t *c;
	int off, ntouch, tbase, i;
	uint8_t b0, b1, b2, status0;

	if (!st)
		return -1;
	st->valid = 0;
	if (!buf || len < 10)
		return -1;

	if (link == DS45_LINK_BT) {
		if (buf[0] == DS4_IN_BT_MINIMAL) {
			/* Controller has not switched to the full report yet. It still
			 * carries every button, just no motion or touch data. */
			off = DS4_OFF_USB;
			if (len < DS4_OFF_USB + 9)
				return -1;
		} else if (buf[0] == DS4_IN_BT) {
			off = DS4_OFF_BT;
			if (len < DS4_OFF_BT + DS4_C_SIZE)
				return -1;
		} else {
			return -1;
		}
	} else {
		if (buf[0] != DS4_IN_USB || len < DS4_OFF_USB + DS4_C_SIZE)
			return -1;
		off = DS4_OFF_USB;
	}

	c = buf + off;

	st->lx = c[DS4_C_LX];
	st->ly = c[DS4_C_LY];
	st->rx = c[DS4_C_RX];
	st->ry = c[DS4_C_RY];

	b0 = c[DS4_C_BUTTONS + 0];
	b1 = c[DS4_C_BUTTONS + 1];
	b2 = c[DS4_C_BUTTONS + 2];

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
	if (b1 & 0x10) st->buttons |= DS45_B_SHARE;
	if (b1 & 0x20) st->buttons |= DS45_B_OPTIONS;
	if (b1 & 0x40) st->buttons |= DS45_B_L3;
	if (b1 & 0x80) st->buttons |= DS45_B_R3;

	if (b2 & 0x01) st->buttons |= DS45_B_PS;
	if (b2 & 0x02) st->buttons |= DS45_B_TOUCHPAD;

	st->l2 = c[DS4_C_L2];
	st->r2 = c[DS4_C_R2];

	/* The minimal Bluetooth report stops right after the triggers. */
	if (link == DS45_LINK_BT && buf[0] == DS4_IN_BT_MINIMAL) {
		for (i = 0; i < 3; i++) {
			st->gyro[i]  = 0;
			st->accel[i] = 0;
		}
		st->touch[0].active = st->touch[1].active = 0;
		st->valid = 1;
		st->sticks_valid = 1;
		return 0;
	}

	for (i = 0; i < 3; i++) {
		st->gyro[i]  = rd16le(c + DS4_C_GYRO  + i * 2);
		st->accel[i] = rd16le(c + DS4_C_ACCEL + i * 2);
	}

	status0      = c[DS4_C_STATUS];
	st->charging = (status0 & DS4_STATUS0_CABLE) ? 1 : 0;
	st->battery  = status0 & DS4_STATUS0_BATTERY;
	if (st->charging) {
		/* Cabled: 0..10 while charging, 11 once the pack is full. */
		st->full = (st->battery >= 11);
		if (st->battery > 10)
			st->battery = 10;
	} else {
		st->full = 0;
		if (st->battery > 9)
			st->battery = 10;
	}

	/* Touch reports follow the common payload: a count byte and then
	 * 9 byte records of {timestamp, point0[4], point1[4]}. */
	st->touch[0].active = st->touch[1].active = 0;
	tbase  = off + DS4_C_SIZE;
	if (tbase < len) {
		ntouch = buf[tbase];
		if (ntouch > 0 && tbase + 9 <= len - 1) {
			decode_touch(buf + tbase + 2, &st->touch[0]);
			decode_touch(buf + tbase + 6, &st->touch[1]);
		}
	}

	st->valid = 1;
	st->sticks_valid = 1;
	return 0;
}

int ds4_build_output(uint8_t *buf, int cap, ds45_link_t link,
                     const ds45_output_t *out)
{
	int size, o;

	size = (link == DS45_LINK_BT) ? DS4_OUT_BT_SIZE : DS4_OUT_USB_SIZE;
	if (cap < size)
		return -1;

	for (o = 0; o < size; o++)
		buf[o] = 0;

	if (link == DS45_LINK_BT) {
		buf[0] = DS4_OUT_BT;
		buf[1] = 0xC0;   /* enable HID + set the poll interval to default */
		buf[2] = 0xA0;
		o = 3;
	} else {
		buf[0] = DS4_OUT_USB;
		o = 1;
	}

	buf[o + 0] = 0x07;                  /* motor + led + led blink valid */
	buf[o + 1] = 0x00;
	buf[o + 2] = 0x00;                  /* reserved                      */
	buf[o + 3] = out->rumble_weak;
	buf[o + 4] = out->rumble_strong;
	buf[o + 5] = out->led_r;
	buf[o + 6] = out->led_g;
	buf[o + 7] = out->led_b;
	buf[o + 8] = 0x00;                  /* blink on  */
	buf[o + 9] = 0x00;                  /* blink off */

	if (link == DS45_LINK_BT) {
		uint8_t seed = DS4_CRC_SEED_OUT;
		uint32_t crc = ds45_crc32(0xFFFFFFFF, &seed, 1);
		crc = ~ds45_crc32(crc, buf, size - 4);
		buf[size - 4] = (uint8_t)(crc & 0xFF);
		buf[size - 3] = (uint8_t)((crc >> 8) & 0xFF);
		buf[size - 2] = (uint8_t)((crc >> 16) & 0xFF);
		buf[size - 1] = (uint8_t)((crc >> 24) & 0xFF);
	}

	return size;
}
