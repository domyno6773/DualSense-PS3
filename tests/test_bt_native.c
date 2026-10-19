/* Compile the actual Bluetooth implementation with fake PS3/VSH APIs.
 * No LDD mocks exist: any attempt to register/insert a pad fails to link.
 * This validates routing/state, not the behavior/ABI of VSH on real hardware. */
#include "../src/passthru.c"

ds45_config_t g_cfg;
ds45_pad_t g_pads[DS45_MAX_PADS];
static CellPadInfo2 info;
static CellPadInfo ids;
static int usb_active, api_missing, info_error, mode, opens, closes, open_port;
static int show_result;
static uint64_t work, raw0, raw1;

void *memset(void *p, int v, size_t n)
{
    unsigned char *s = p;
    while (n--) *s++ = (unsigned char)v;
    return p;
}
void *memcpy(void *p, const void *q, size_t n)
{
    unsigned char *s = p;
    const unsigned char *t = q;
    while (n--) *s++ = *t++;
    return p;
}
int cellPadGetInfo2(CellPadInfo2 *p) { *p = info; return info_error; }
int cellPadGetInfo(CellPadInfo *p) { *p = ids; return info_error; }
void ds45_boot_mark(const char *s) { (void)s; }
void ds45_boot_mark_hex(const char *s, uint32_t v) { (void)s; (void)v; }
void ds45_notify(const char *s) { (void)s; }
int ds45_usb_pads_active(void) { return usb_active; }
int ds45_bt_pads_active(void)
{
    int i, n = 0;
    for (i = 0; i < DS45_MAX_PADS; i++)
        n += g_pads[i].in_use && g_pads[i].link == DS45_LINK_BT;
    return n;
}
static uint32_t running_mode(void) { return (uint32_t)mode; }
static int32_t show_menu(int dev, int port, int intr)
{
    if (dev != 0 || intr != 0) return -2;
    opens++; open_port = port; return show_result;
}
static void end_menu(void) { closes++; }
void *ds45_resolve(const char *s, uint32_t nid)
{
    (void)s;
    if (api_missing) return 0;
    if (nid == NID_VSHMAIN_RUNNING_MODE) return (void *)running_mode;
    if (nid == NID_VSHMAIN_SHOW_INGAME_XMB) return (void *)show_menu;
    if (nid == NID_VSHMAIN_END_INGAME_XMB) return (void *)end_menu;
    return 0;
}
uint64_t test_peek(uint64_t addr)
{
    if (addr == BT_WORK_PTR_493) return work;
    if (addr == work + BT_REPORT_OFFSET) return raw0;
    if (addr == work + BT_REPORT_OFFSET + 8) return raw1;
    return 0;
}
static void connect_pad(int port)
{
    info.port_status[port] = DS45_STATUS_CONNECTED;
    ids.vendor_id[port] = 0x054c; ids.product_id[port] = 0x0ce6;
}
static void reset(void)
{
    int i;
    memset(&info, 0, sizeof(info)); memset(&ids, 0, sizeof(ids));
    memset(&g_cfg, 0, sizeof(g_cfg)); memset(g_pads, 0, sizeof(g_pads));
    for (i = 0; i < DS45_MAX_PADS; i++) g_pads[i].handle = -1;
    g_cfg.bt_source_port = -1; g_cfg.max_pads = 1; g_cfg.poll_ms = 10;
    usb_active = api_missing = info_error = opens = closes = show_result = 0;
    mode = 1; open_port = -1;
    s_resolve_tried = 0; s_bt_work = 0;
    work = 0x800000000040b5e0ULL;
    raw0 = 0xA101808080800800ULL; raw1 = 0;
    connect_pad(0);
}
static void sample(int pressed)
{
    raw1 = (uint64_t)pressed << 56;
    passthru_poll();
}
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

int run_tests(void)
{
    int i;
    reset();
    CHECK(passthru_scan() == 1);
    CHECK(passthru_scan() == 0);
    CHECK(ds45_bt_pads_active() == 1);
    CHECK(g_pads[0].port == 0 && g_pads[0].src_port == 0);
    CHECK(g_pads[0].handle == -1);
    sample(1); sample(1); CHECK(opens == 0); /* held during connection */
    sample(0); sample(1); CHECK(opens == 1 && open_port == 0);
    for (i = 0; i < 20; i++) sample(1);
    CHECK(opens == 1 && closes == 0);
    info.system_info = 1; sample(0); sample(1);
    CHECK(closes == 1); /* close with a second PS press */
    info.system_info = 0; sample(0); sample(1); CHECK(opens == 2);
    info.system_info = 1; sample(0);
    info.system_info = 0; sample(0); /* Circle closes through the native pad */
    sample(1); CHECK(opens == 3 && closes == 1);
    passthru_release(); CHECK(ds45_bt_pads_active() == 0);

    reset(); CHECK(passthru_scan() == 1); sample(0);
    mode = 0; sample(1); CHECK(opens == 0); /* main XMB */
    mode = 1; info.system_info = 1; sample(0); sample(1);
    CHECK(opens == 0 && closes == 0); /* unrelated system dialog */

    reset(); CHECK(passthru_scan() == 1); sample(0);
    raw0 = 0xA131808080800800ULL; sample(1); CHECK(opens == 0);
    raw0 = 0xA111808080800800ULL; sample(1); CHECK(opens == 0);
    raw0 = 0xA101808080800800ULL; sample(1); CHECK(opens == 1);
    info.port_status[0] = 0; sample(0); CHECK(ds45_bt_pads_active() == 0);
    connect_pad(2); CHECK(passthru_scan() == 1); sample(0); sample(1);
    CHECK(open_port == 2 && g_pads[0].handle == -1); /* never steal a port */

    reset(); usb_active = 1; CHECK(passthru_scan() == 0);
    reset(); ids.product_id[0] = 0x0268; CHECK(passthru_scan() == 0);
    reset(); connect_pad(1); CHECK(passthru_scan() == 0); /* shared buffer */
    reset(); info_error = -1; CHECK(passthru_scan() == 0);
    reset(); api_missing = 1; CHECK(passthru_scan() == 1);
    sample(0); sample(1); CHECK(opens == 0 && g_pads[0].handle == -1);
    reset(); work = 0; CHECK(passthru_scan() == 1);
    sample(0); sample(1); CHECK(opens == 0);
    reset(); CHECK(passthru_scan() == 1); sample(0); show_result = -1;
    sample(1); CHECK(!g_pads[0].bt_menu_requested);
    reset(); CHECK(passthru_scan() == 1); sample(0); sample(1);
    for (i = 0; i < 201; i++) sample(0);
    CHECK(!g_pads[0].bt_menu_requested); /* accepted call, overlay absent */
    sample(1); CHECK(opens == 2);
    reset(); CHECK(passthru_scan() == 1); sample(0);
    connect_pad(1); sample(1); CHECK(opens == 0); /* second pad connected later */
    info.port_status[1] = 0; sample(1); CHECK(opens == 0);
    sample(0); sample(1); CHECK(opens == 1); /* rearm after ambiguity */
    reset(); connect_pad(1); ids.product_id[1] = 0x0268;
    CHECK(passthru_scan() == 0); /* DS3 also shares the receive buffer */
    return 0;
}
