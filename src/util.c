/*
 * DS45PAD - odds and ends: XMB notifications, file reading, CRC32 and the
 * remap helpers.
 *
 * There is deliberately no allocator here. Every controller slot lives in a
 * static array, so the plugin never touches the heap and cannot leak or
 * fragment the VSH's memory.
 */

#include <string.h>
#include <cell/cell_fs.h>

#include "ds45pad.h"
#include "hook.h"

/* ------------------------------------------------------------------------ */
/* XMB notifications                                                         */
/*                                                                           */
/* vshtask's notification entry point has no SDK stub, so it is resolved from */
/* the import table at runtime. Only the VSH process imports vshtask, so in   */
/* the in-game build the lookup simply comes back empty and notifications go  */
/* nowhere - which is the right behaviour there anyway.                      */
/* ------------------------------------------------------------------------ */

static int (*vshtask_notify)(int, const char *);
static int  notify_tried;

void ds45_notify(const char *msg)
{
	if (!notify_tried) {
		notify_tried = 1;
		vshtask_notify = (int (*)(int, const char *))
		                 ds45_resolve("vshtask", NID_VSHTASK_NOTIFY);
	}

	if (vshtask_notify)
		vshtask_notify(0, msg);
}

/* ------------------------------------------------------------------------ */
/* Boot trace                                                                */
/*                                                                           */
/* A marker is written before each startup step that touches the system, and  */
/* flushed immediately by closing the file. If the console hangs while the    */
/* plugin loads there is no other way to find out how far it got - so this is */
/* deliberately dumb and synchronous rather than buffered.                    */
/* ------------------------------------------------------------------------ */

static void boot_write(const char *line, int len)
{
	int fd;
	uint64_t written;

	if (cellFsOpen(DS45_BOOT_FILE,
	               CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_APPEND,
	               &fd, NULL, 0) == CELL_FS_SUCCEEDED) {
		cellFsWrite(fd, line, (uint64_t)len, &written);
		cellFsClose(fd);
	}
}

void ds45_boot_mark(const char *stage)
{
	char line[64];
	int i = 0;

	while (stage[i] && i < (int)sizeof(line) - 2) {
		line[i] = stage[i];
		i++;
	}
	line[i++] = '\n';

	boot_write(line, i);
}

/* Same, with a value appended in hex - used to record what the system actually
 * reports back for the virtual pad, which is the only way to tell a working
 * registration from one that silently fell back. */
void ds45_boot_mark_hex(const char *stage, uint32_t value)
{
	static const char hex[] = "0123456789ABCDEF";
	char line[80];
	int i = 0, d;

	while (stage[i] && i < 60) {
		line[i] = stage[i];
		i++;
	}

	line[i++] = '=';
	line[i++] = '0';
	line[i++] = 'x';
	for (d = 28; d >= 0; d -= 4)
		line[i++] = hex[(value >> d) & 0xF];
	line[i++] = '\n';

	boot_write(line, i);
}

/* ------------------------------------------------------------------------ */
/* CRC32 (IEEE, reflected) - Bluetooth output reports are checksummed         */
/* ------------------------------------------------------------------------ */

uint32_t ds45_crc32(uint32_t crc, const uint8_t *data, int len)
{
	int i, b;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
	}

	return crc;
}

/* ------------------------------------------------------------------------ */
/* File reading                                                              */
/* ------------------------------------------------------------------------ */

int ds45_read_file(const char *path, char *out, int cap)
{
	int fd;
	uint64_t nread = 0;

	if (cellFsOpen(path, CELL_FS_O_RDONLY, &fd, NULL, 0) != CELL_FS_SUCCEEDED)
		return -1;

	cellFsRead(fd, out, (uint64_t)cap, &nread);
	cellFsClose(fd);

	return (int)nread;
}

/* ------------------------------------------------------------------------ */
/* Remapping                                                                 */
/* ------------------------------------------------------------------------ */

static const struct {
	const char *name;
	ds45_map_t  map;
} map_names[] = {
	{ "NONE",     DS45_MAP_NONE     },
	{ "CROSS",    DS45_MAP_CROSS    },
	{ "CIRCLE",   DS45_MAP_CIRCLE   },
	{ "SQUARE",   DS45_MAP_SQUARE   },
	{ "TRIANGLE", DS45_MAP_TRIANGLE },
	{ "L1",       DS45_MAP_L1       },
	{ "R1",       DS45_MAP_R1       },
	{ "L2",       DS45_MAP_L2       },
	{ "R2",       DS45_MAP_R2       },
	{ "L3",       DS45_MAP_L3       },
	{ "R3",       DS45_MAP_R3       },
	{ "START",    DS45_MAP_START    },
	{ "SELECT",   DS45_MAP_SELECT   },
	{ "PS",       DS45_MAP_PS       },
	{ "UP",       DS45_MAP_UP       },
	{ "RIGHT",    DS45_MAP_RIGHT    },
	{ "DOWN",     DS45_MAP_DOWN     },
	{ "LEFT",     DS45_MAP_LEFT     },
};

ds45_map_t ds45_parse_map(const char *name)
{
	unsigned i;

	for (i = 0; i < sizeof(map_names) / sizeof(map_names[0]); i++)
		if (strcmp(name, map_names[i].name) == 0)
			return map_names[i].map;

	return DS45_MAP_NONE;
}

void ds45_apply_map(ds45_map_t m, uint16_t *button)
{
	switch (m) {
	case DS45_MAP_CROSS:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_CROSS;
		button[DS45_BTN_PRESS_CROSS] = 255;
		break;
	case DS45_MAP_CIRCLE:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_CIRCLE;
		button[DS45_BTN_PRESS_CIRCLE] = 255;
		break;
	case DS45_MAP_SQUARE:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_SQUARE;
		button[DS45_BTN_PRESS_SQUARE] = 255;
		break;
	case DS45_MAP_TRIANGLE:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_TRIANGLE;
		button[DS45_BTN_PRESS_TRIANGLE] = 255;
		break;
	case DS45_MAP_L1:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_L1;
		button[DS45_BTN_PRESS_L1] = 255;
		break;
	case DS45_MAP_R1:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_R1;
		button[DS45_BTN_PRESS_R1] = 255;
		break;
	case DS45_MAP_L2:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_L2;
		button[DS45_BTN_PRESS_L2] = 255;
		break;
	case DS45_MAP_R2:
		button[DS45_BTN_DIGITAL2] |= DS45_CTRL_R2;
		button[DS45_BTN_PRESS_R2] = 255;
		break;
	case DS45_MAP_L3:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_L3;
		break;
	case DS45_MAP_R3:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_R3;
		break;
	case DS45_MAP_START:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_START;
		break;
	case DS45_MAP_SELECT:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_SELECT;
		break;
	case DS45_MAP_PS:
		button[DS45_BTN_LDD]      |= DS45_CTRL_LDD_PS;
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_PS;
		break;
	case DS45_MAP_UP:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_UP;
		button[DS45_BTN_PRESS_UP] = 255;
		break;
	case DS45_MAP_RIGHT:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_RIGHT;
		button[DS45_BTN_PRESS_RIGHT] = 255;
		break;
	case DS45_MAP_DOWN:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_DOWN;
		button[DS45_BTN_PRESS_DOWN] = 255;
		break;
	case DS45_MAP_LEFT:
		button[DS45_BTN_DIGITAL1] |= DS45_CTRL_LEFT;
		button[DS45_BTN_PRESS_LEFT] = 255;
		break;
	default:
		break;
	}
}

int ds45_active_pads(void)
{
	int i, n = 0;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (g_pads[i].in_use)
			n++;

	return n;
}

int ds45_usb_pads_active(void)
{
	int i, n = 0;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (g_pads[i].in_use && g_pads[i].link == DS45_LINK_USB)
			n++;

	return n;
}

int ds45_bt_pads_active(void)
{
	int i, n = 0;

	for (i = 0; i < DS45_MAX_PADS; i++)
		if (g_pads[i].in_use && g_pads[i].link == DS45_LINK_BT)
			n++;

	return n;
}
