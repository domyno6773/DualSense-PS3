/*
 * DS45PAD - diagnostics.
 *
 * The Bluetooth passthrough path has one genuinely unknown: what, if
 * anything, the firmware's generic HID driver does with the buttons a
 * DualShock 3 does not have - PS, touchpad click, mute. That cannot be
 * answered from documentation, only measured on the console.
 *
 * Turning DIAG on writes every pad code that changes to
 * /dev_hdd0/tmp/ds45pad_diag.txt, so pressing a button and reading the file
 * tells you exactly which code and bit to put in BT_PS_CODE / BT_PS_MASK.
 *
 * cellPadGetRawData() would be worth sampling too, since it can carry codes
 * the cooked path drops, but SDK 4.75 neither declares it nor exports it from
 * libio_stub, so there is nothing to link against.
 */

#include <string.h>
#include <cell/cell_fs.h>
#include <cell/pad.h>

#include "ds45pad.h"

/* Writes are buffered and only committed when the buffer is nearly full.
 *
 * The first version opened, wrote and closed the file on every change, from a
 * thread running at 250 Hz. That competes with the VSH for disk I/O hard
 * enough to stop multiMAN from loading at all, which is a poor trade for a
 * debug log. The total is capped too, so leaving DIAG on cannot fill the
 * drive or slow the console down indefinitely. */
#define DIAG_BUF       4096
#define DIAG_FLUSH_AT  3072
#define DIAG_MAX_BYTES (64 * 1024)

static char diag_buf[DIAG_BUF];
static int  diag_len;
static int  diag_written;
static int  diag_full;

static uint16_t last_cooked[DS45_MAX_PORT_NUM][64];
static int      seeded[DS45_MAX_PORT_NUM];

/* In the in-game build diag_sticks() runs on the game's own pad-reading
 * threads, so this buffer has more than one writer - and none of it was written
 * for that. The index advance in put() is a plain read-modify-write: with three
 * writers past the bounds check at the same moment, the third stores past the
 * end of diag_buf, and what lies immediately after it is the very state that
 * keeps the rest of this file in bounds. commit() is worse, resetting the index
 * while another thread is mid-write, and doing file I/O on the thread a game
 * reads input on.
 *
 * A try lock, not a blocking one: diagnostics must never stall a game's input
 * thread, and a dropped log line costs nothing. */
static volatile uint32_t diag_busy;

static int diag_claim(void)
{
	return __sync_bool_compare_and_swap(&diag_busy, 0, 1);
}

static void diag_release(void)
{
	__sync_synchronize();
	diag_busy = 0;
}

static void commit(void)
{
	int fd;
	uint64_t written;

	if (diag_len <= 0)
		return;

	if (cellFsOpen(DS45_DIAG_FILE,
	               CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_APPEND,
	               &fd, NULL, 0) == CELL_FS_SUCCEEDED) {
		cellFsWrite(fd, diag_buf, (uint64_t)diag_len, &written);
		cellFsClose(fd);
		diag_written += diag_len;
		if (diag_written >= DIAG_MAX_BYTES)
			diag_full = 1;
	}

	diag_len = 0;
}

/* Only commits when the buffer is nearly full; callers no longer force it. */
static void flush(void)
{
	if (diag_len >= DIAG_FLUSH_AT)
		commit();
}

static void put(const char *s)
{
	if (diag_full)
		return;

	while (*s) {
		if (diag_len >= DIAG_BUF - 1)
			commit();
		diag_buf[diag_len++] = *s++;
	}
}

static void put_hex(uint32_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	char tmp[9];
	int i;

	if (digits > 8) digits = 8;
	tmp[digits] = 0;
	for (i = digits - 1; i >= 0; i--) {
		tmp[i] = hex[v & 0xF];
		v >>= 4;
	}

	put(tmp);
}

static void put_dec(int v)
{
	char tmp[12];
	int i = 11;

	tmp[i] = 0;
	if (v < 0) {
		put("-");
		v = -v;
	}

	do {
		tmp[--i] = (char)('0' + (v % 10));
		v /= 10;
	} while (v && i > 0);

	put(&tmp[i]);
}

void diag_msg(const char *msg)
{
	if (!diag_claim())
		return;

	put(msg);
	put("\n");
	commit();     /* one-off messages are worth writing straight away */
	diag_release();
}

void diag_dump_ports(void)
{
	CellPadInfo2 info;
	int port;

	if (cellPadGetInfo2(&info) != CELL_PAD_OK) {
		diag_msg("cellPadGetInfo2 falhou");
		return;
	}

	if (!diag_claim())
		return;

	put("--- portas ---\n");
	put("max="); put_dec((int)info.max_connect);
	put(" conectados="); put_dec((int)info.now_connect);
	put("\n");

	for (port = 0; port < DS45_MAX_PORT_NUM; port++) {
		if (!(info.port_status[port] & DS45_STATUS_CONNECTED))
			continue;

		put("porta "); put_dec(port);
		put("  status=0x");   put_hex(info.port_status[port], 4);
		put("  setting=0x");  put_hex(info.port_setting[port], 4);
		put("  cap=0x");      put_hex(info.device_capability[port], 4);
		put("  type=");       put_dec((int)info.device_type[port]);
		put(info.device_capability[port] & DS45_CAP_PS3_CONFORMITY
		    ? "  [CONFORME]" : "  [GENERICO]");
		put("\n");
	}

	put("\n");
	commit();
	diag_release();
}

static void dump_changes(const char *tag, int port, const CellPadData *data,
                         uint16_t *last, int seed)
{
	int code, changed = 0;

	for (code = 0; code < 64 && code < data->len; code++) {
		uint16_t v = data->button[code];

		if (v == last[code])
			continue;

		last[code] = v;
		if (seed)
			continue;

		if (!changed) {
			put(tag); put(" porta "); put_dec(port); put(":");
			changed = 1;
		}

		put("  [");   put_dec(code);
		put("]=0x");  put_hex(v, 4);
	}

	if (changed) {
		put("\n");
		flush();
	}
}

/* Observe the sample already read by passthrough; a second GetData can
 * consume a transition before passthrough sees it. */
void diag_observe(int port, const void *sample)
{
	const CellPadData *data = (const CellPadData *)sample;
	if (!data || port < 0 || port >= DS45_MAX_PORT_NUM || data->len <= 0)
		return;
	if (!diag_claim())
		return;
	dump_changes("data", port, data, last_cooked[port], !seeded[port]);
	seeded[port] = 1;
	diag_release();
}

void diag_flush(void)
{
	if (!diag_claim())
		return;
	commit();
	diag_release();
}

/* What the plugin itself decoded from the controller, before any of it reaches
 * libpad. This is the only way to tell "the controller never reported the
 * button" apart from "we reported it and the system ignored it". */
void diag_state(const ds45_pad_t *pad)
{
	static uint32_t last[DS45_MAX_PADS];
	static int seeded_state[DS45_MAX_PADS];
	int slot = (int)(pad - g_pads);
	uint32_t b;

	if (slot < 0 || slot >= DS45_MAX_PADS || !pad->state.valid)
		return;

	b = pad->state.buttons;
	if (seeded_state[slot] && b == last[slot])
		return;

	if (!diag_claim())
		return;

	last[slot] = b;
	seeded_state[slot] = 1;

	put("btn slot "); put_dec(slot);
	put(" = 0x"); put_hex(b, 8);
	if (b & DS45_B_PS)       put("  PS");
	if (b & DS45_B_TOUCHPAD) put("  TOUCH");
	if (b & DS45_B_MUTE)     put("  MUTE");
	if (b & DS45_B_SHARE)    put("  SHARE");
	if (b & DS45_B_OPTIONS)  put("  OPTIONS");
	put("\n");
	flush();
	diag_release();
}

/* Stick sampling.
 *
 * Reported symptom this exists for: a small, always-identical sector of one
 * stick failing while the rest of the travel behaves. Simulation of the
 * transform found no discontinuity anywhere on the surface, so the answer has
 * to come from the console: what the source actually reports at that angle,
 * and what leaves the correction.
 *
 * Only near-edge samples are logged, throttled, because that is where the
 * report is and logging the whole surface would flood the file. */
#define DIAG_STICK_EDGE   80    /* percent of full travel worth logging */
#define DIAG_STICK_EVERY  5     /* keep one sample in this many         */

void diag_sticks(uint8_t in_x, uint8_t in_y, uint8_t out_x, uint8_t out_y,
                 const ds45_axis_cal_t *cx, const ds45_axis_cal_t *cy)
{
	static int skip;
	int dx = (int)in_x - 128;
	int dy = (int)in_y - 128;
	int mag2 = dx * dx + dy * dy;
	int edge = (128 * DIAG_STICK_EDGE) / 100;

	if (mag2 < edge * edge)
		return;
	if (!diag_claim())
		return;
	if (++skip < DIAG_STICK_EVERY) {
		diag_release();
		return;
	}
	skip = 0;

	put("stk in=");   put_dec(in_x);  put(",");  put_dec(in_y);
	put("  out=");    put_dec(out_x); put(",");  put_dec(out_y);
	put("  calX=");   put_dec(cx->min); put("/"); put_dec(cx->ctr);
	put("/");         put_dec(cx->max);
	put("  calY=");   put_dec(cy->min); put("/"); put_dec(cy->ctr);
	put("/");         put_dec(cy->max);
	put("\n");
	flush();
	diag_release();
}

void diag_watch(void)
{
	CellPadInfo2 info;
	CellPadData  data;
	int port;

	if (cellPadGetInfo2(&info) != CELL_PAD_OK)
		return;

	if (!diag_claim())
		return;

	for (port = 0; port < DS45_MAX_PORT_NUM; port++) {
		int slot, bound = 0;
		if (!(info.port_status[port] & DS45_STATUS_CONNECTED))
			continue;

		/* Skip our own virtual pads: watching those would just echo back
		 * whatever we injected a moment ago. */
		if (info.device_type[port] == DS45_DEV_TYPE_LDD)
			continue;
		for (slot = 0; slot < DS45_MAX_PADS; slot++)
			if (g_pads[slot].in_use && g_pads[slot].link == DS45_LINK_BT &&
			    g_pads[slot].src_port == port)
				bound = 1;
		if (bound)
			continue;

		memset(&data, 0, sizeof(data));
		if (cellPadGetData((uint32_t)port, &data) == CELL_PAD_OK && data.len > 0) {
			dump_changes("data", port, &data, last_cooked[port], !seeded[port]);
			seeded[port] = 1;
		}
	}
	diag_release();
}
