/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Header and navigation (firmware/src/ui_nav.c, task.md) on the real UI: Bluetooth's status in the header only on the
 * Bluetooth page (and its PIN everywhere), its changes as toasts, the Settings row. */
#include <stdint.h>
static struct { char t[24]; int32_t x0, x1; } hd[32];
static uint32_t hd_n;
static void hd_text(int32_t x0, int32_t y0, int32_t x1, const char *t)   /* (gfx.c GFX_HOOK_TEXT): the header's texts */
{
    uint32_t k;
    if (y0 >= 30 || hd_n >= 32u) return;
    for (k = 0; k < 23u && t[k]; k++) hd[hd_n].t[k] = t[k];
    hd[hd_n].t[k] = 0; hd[hd_n].x0 = x0; hd[hd_n].x1 = x1; hd_n++;
}
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hd_text(x0, y0, x1, s)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void head(void) { hd_n = 0; ui.force = 1; draw_head(); }
static int head_has(const char *s)
{
    for (uint32_t i = 0; i < hd_n; i++)
        if (str_eq(hd[i].t, s)) return 1;
    return 0;
}
static int head_starts(const char *s)
{
    for (uint32_t i = 0; i < hd_n; i++)
        if (str_prefix(hd[i].t, s)) return 1;
    return 0;
}

#if MELODEE_BLE
static int test_ble_header(void)
{
    int bad = 0;
    ui_power_on();
    memset(&mb, 0, sizeof mb); mb.cal = 1; mb.on = 1;
    smp_passkey = BLE_NO_PASSKEY;
    ui.menu = 0;
    go_title("ARP");
    head();
    bad += check("BLE on, not connected, on a page: no BLE status or tag in the header",
                 !head_starts("BLE") && !head_has("SCANNING") && head_has("ARP"));
    mb.ready = 1;
    head();
    bad += check("..connected: no BLE tag, the page's name stays", !head_starts("BLE") && head_has("ARP"));
    mb.ready = 0;
    ui.menu = 3;
    head();
    bad += check("on the Bluetooth page: its status in the header", head_has("BLE AVAILABLE"));
    ui.menu = 0;
    mb.attempt = 1; bcen.st = BLE_CS_SETUP; bsmp.init = 1; bsmp.st = I_CONFIRM; smp_passkey = 42;
    head();
    bad += check("pairing: the PIN on any page", head_has("PIN 000042"));
    mb.attempt = 0; smp_passkey = BLE_NO_PASSKEY;
    return bad;
}

static int test_ble_toasts(void)
{
    int bad = 0;
    ui_power_on();
    memset(&mb, 0, sizeof mb); mb.cal = 1; mb.on = 1;
    memset(&nav_ble, 0, sizeof nav_ble);
    ui.menu = 0; ui.msg_t = 0;
    nav_ble_toasts();
    bad += check("BLE on, nothing changed: no toast", !ui.msg_t);
    mb.ready = 1; nav_ble_toasts();
    bad += check("connected: BLE CONNECTED", msg_is("BLE CONNECTED"));
    ui.msg_t = 0; nav_ble_toasts();
    bad += check("..once", !ui.msg_t);
    mb.ready = 0; nav_ble_toasts();
    bad += check("disconnected: BLE DISCONNECTED", msg_is("BLE DISCONNECTED"));
    ui.msg_t = 0; str_cpy(mb.failure, "PAIRING FAILED", sizeof mb.failure); nav_ble_toasts();
    bad += check("a failure: its words", msg_is("PAIRING FAILED"));
    ui.msg_t = 0; mb.failure[0] = 0; ui.menu = 3; mb.ready = 1; nav_ble_toasts();
    bad += check("on the Bluetooth page: no toast (it shows the status)", !ui.msg_t);
    ui.menu = 0;
    return bad;
}
#endif

int main(void)
{
    int bad = 0;
#if MELODEE_BLE
    bad += test_ble_header();
    bad += test_ble_toasts();
#endif
    printf(bad ? "nav: %d FAILED\n" : "nav: all ok\n", bad);
    return bad != 0;
}
