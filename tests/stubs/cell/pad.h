#ifndef DS45_TEST_PAD_H
#define DS45_TEST_PAD_H
#include <stdint.h>
#define CELL_PAD_OK 0
typedef struct {
    uint32_t max_connect, now_connect, info;
    uint16_t vendor_id[127], product_id[127];
    uint8_t status[127];
} CellPadInfo;
typedef struct {
    uint32_t max_connect, now_connect, system_info;
    uint32_t port_status[7], port_setting[7];
    uint32_t device_capability[7], device_type[7];
} CellPadInfo2;
int cellPadGetInfo(CellPadInfo *);
int cellPadGetInfo2(CellPadInfo2 *);
#endif
