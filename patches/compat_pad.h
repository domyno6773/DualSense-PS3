/*
 * Compatibility layer for building DS45PAD with the Cell SDK 192.001.
 *
 * That SDK predates cellPadGetInfo2() and cellPadSetPortSetting(): neither the
 * headers nor libio_stub.a know them. The plugin only needs a handful of
 * fields, so cellPadGetInfo2() is rebuilt on top of cellPadGetInfo(), which
 * exists in every SDK and whose CellPadInfo has room for all 127 ports.
 *
 * What cellPadGetInfo() cannot tell us is the device capability and type. The
 * only decision the plugin takes from them is "is this port a generic pad or
 * one of our own virtual pads", and cellPadGetInfo() answers that directly:
 * status CELL_PAD_STATUS_LDD_CONNECTED (5) is an LDD pad.
 *
 * Force-included with -include, so no original source file is edited for it.
 */
#ifndef DS45_COMPAT_PAD_H
#define DS45_COMPAT_PAD_H

#include <string.h>
#include <cell/pad.h>

#define DS45_COMPAT_PORTS 7

typedef struct CellPadInfo2 {
	uint32_t max_connect;
	uint32_t now_connect;
	uint32_t system_info;
	uint32_t port_status[DS45_COMPAT_PORTS];
	uint32_t port_setting[DS45_COMPAT_PORTS];
	uint32_t device_capability[DS45_COMPAT_PORTS];
	uint32_t device_type[DS45_COMPAT_PORTS];
} CellPadInfo2;

static inline int32_t cellPadGetInfo2(CellPadInfo2 *info2)
{
	CellPadInfo info;
	int32_t r;
	int i;

	if (!info2)
		return -1;

	r = cellPadGetInfo(&info);
	if (r != 0)
		return r;

	memset(info2, 0, sizeof(*info2));
	info2->max_connect = info.max_connect;
	info2->now_connect = info.now_connect;
	info2->system_info = info.info;

	for (i = 0; i < DS45_COMPAT_PORTS; i++) {
		uint32_t st = info.status[i];

		info2->port_status[i] = st;
		/* 5 = LDD, 1 = anything else the firmware drives itself */
		info2->device_type[i] = (st == CELL_PAD_STATUS_LDD_CONNECTED) ? 5 : 1;
	}

	return 0;
}

/* The extended report (pressure, sensors) is only requested for a source pad
 * that claims it, and the generic pad never does. Nothing to do. */
static inline int32_t cellPadSetPortSetting(uint32_t port_no, uint32_t setting)
{
	(void)port_no;
	(void)setting;
	return 0;
}

#endif /* DS45_COMPAT_PAD_H */
