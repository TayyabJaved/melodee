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

/* SELECT once round fam's pages from the button: every page fam's, the button lit; back where it started */
static int walk(uint32_t fam, uint32_t *n_out, const char **first)
{
    uint32_t start, n = 0, ok = 1;
    open_family(fam);
    start = ui.page;
    if (first) *first = cur_page()->title;
    do {
        ok &= nav_owner(ui.page) == fam && cur_fam() == fam;
        turn(EN_SELECT, 1);
        n++;
    } while (ui.page != start && n < 64u);
    *n_out = n;
    return ok && n < 64u;
}
static int test_pages_one_home(void)
{
    static const uint32_t ENG[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, 3u, ENGI_DRUM};
    static const char *const NM[] = {"Prophet", "FM6", "CZ-1", "SID", "DRUM"};
    int bad = 0;
    char what[96];
    for (uint32_t e = 0; e < 5u; e++) {
        uint32_t n, i, ok = 1;
        const char *first;
        ui_power_on();
        set_engine_of(TSEL, ENG[e]);
        snprintf(what, sizeof what, "%s: ENV + SELECT round: its pages only, ENV lit", NM[e]);
        bad += check(what, walk(FAM_ENV, &n, &first) && n >= 1u);
        snprintf(what, sizeof what, "%s: LFO + SELECT round: LFO lit, the track LFO and MOD last", NM[e]);
        bad += check(what, walk(FAM_LFO, &n, &first) && n >= 4u);
        go_home();
        snprintf(what, sizeof what, "%s: EDIT + SELECT round: never an envelope or LFO page, EDIT lit", NM[e]);
        bad += check(what, walk(FAM_EDIT, &n, &first));
        for (i = 0; i < NPAGES; i++)                    /* no section holds an envelope / LFO page */
            if (page_visible(i) && nav_owner(i) != FAM_EDIT && PAGES[i].fam == FAM_EDIT) {
                ui.page = (uint8_t)i; ui.home = 0;
                ok &= !sec_on();
            }
        snprintf(what, sizeof what, "%s: the engine's envelope / LFO pages are in no section", NM[e]);
        bad += check(what, ok);
    }
    return bad;
}
static int test_edit_button(void)
{
    int bad = 0;
    ui_power_on();
    set_engine_of(TSEL, ENGI_PROPHET);
    open_family(FAM_LFO);
    press(B_EDIT);
    bad += check("Prophet: LFO, then EDIT: an EDIT page, EDIT lit, no map",
                 nav_owner(ui.page) == FAM_EDIT && cur_fam() == FAM_EDIT && !smap.on);
    press(B_EDIT);
    bad += check("..EDIT again: the section map", smap.on);
    press(B_EDIT);
    bad += check("..again: closed", !smap.on);
    open_family(FAM_ENV);
    head();
    bad += check("ENV on the Prophet: the header says P5 FLT ENV (no section shortening)", head_has("P5 FLT ENV"));
    open_family(FAM_LFO);
    head();
    bad += check("LFO on the Prophet: P5 LFO", head_has("P5 LFO"));
    go_title("LFO");
    head();
    bad += check("the track LFO: TRACK LFO", head_has("TRACK LFO"));
    go_title("LFO DEST");
    head();
    bad += check("..its destinations: TRACK DEST", head_has("TRACK DEST"));
    go_title("MOD");
    head();
    bad += check("..MOD stays MOD", head_has("MOD"));
    return bad;
}
static int test_cz_envelopes(void)
{
    int bad = 0;
    ui_power_on();
    set_engine_of(TSEL, ENGI_CZ);
    open_family(FAM_ENV);
    bad += check("CZ-1: ENV opens Line 1's pitch envelope", str_eq(cur_page()->title, "C1 PIT R1-4"));
    turn(EN_SELECT, 1);
    bad += check("..SELECT: its next page (R5-8), ENV lit", str_eq(cur_page()->title, "C1 PIT R5-8") && cur_fam() == FAM_ENV);
    turn(EN_PRESET, 1);
    bad += check("..PRESETS: the next envelope (wave)", str_eq(cur_page()->title, "C1 WAV R1-4"));
    turn(EN_PRESET, -1);
    bad += check("..PRESETS back: the pitch envelope's first page", str_eq(cur_page()->title, "C1 PIT R1-4"));
    turn(EN_SELECT, 1);
    open_family(FAM_ENV);
    bad += check("..ENV again from R5-8: the next envelope", str_eq(cur_page()->title, "C1 WAV R1-4"));
    go_home();
    set_engine_of(TSEL, ENGI_FM6);
    open_family(FAM_ENV);
    {
        uint32_t op = fm6_opsel;
        turn(EN_PRESET, 1);
        bad += check("FM6: PRESETS on EG RATE picks the next operator", fm6_opsel == (op + 1u) % 6u &&
                     str_eq(cur_page()->title, "EG RATE"));
    }
    return bad;
}

static int head_overlap(void)                           /* two header texts on the same columns */
{
    for (uint32_t i = 0; i < hd_n; i++)
        for (uint32_t j = i + 1u; j < hd_n; j++)
            if (hd[i].x0 < hd[j].x1 && hd[j].x0 < hd[i].x1) return 1;
    return 0;
}
/* every visible page: "k/n" when its button has two or more, k its place in what SELECT walks, nothing overlapping */
static int pos_sweep(const char *what, int crowd)
{
    uint32_t i, k, n, bad_pages = 0, shown = 0;
    char want[12];
    for (i = 0; i < NPAGES; i++) {
        if (!page_visible(i) || PAGES[i].graph == GR_MOTION) continue;
        ui.home = 0; ui.page = (uint8_t)i;
        if (crowd) { song.rec = (uint8_t)(1u << song.sel); song.octave = 2; ui_message("SAVED"); }
        n = nav_pos(&k);
        head();
        snprintf(want, sizeof want, "%u/%u", (unsigned)k + 1u, (unsigned)n);
        if (n >= 2u ? !head_has(want) : head_starts("1/")) bad_pages++;
        if (head_overlap()) bad_pages++;
        shown += n >= 2u;
        if (bad_pages == 1u && (n >= 2u ? !head_has(want) : 1)) {
            printf("  (%s: page %s, want %s; header:", what, PAGES[i].title, want);
            for (uint32_t j = 0; j < hd_n; j++) printf(" [%s %d-%d]", hd[j].t, hd[j].x0, hd[j].x1);
            printf(")\n");
            bad_pages++;
        }
    }
    song.rec = 0; song.octave = 0; ui.msg_t = 0;
    return check(what, !bad_pages && shown > 20u);
}
static int test_page_position(void)
{
    static const uint32_t ENG[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, 3u, ENGI_DRUM};
    int bad = 0;
    uint32_t e;
    ui_power_on();
    go_title("ARP");
    head();
    bad += check("ARP: 1/3 after the BPM (ARP, ARP 2, GEN)", head_has("1/3"));
    turn(EN_SELECT, 1); head();
    bad += check("..SELECT: 2/3", head_has("2/3"));
    ui_message("SAVED"); head();
    bad += check("..with a message showing: still 2/3", head_has("2/3"));
    ui.msg_t = 0;
    go_home(); head();
    bad += check("HOME: 1/2 (Stage, the chord view)", head_has("1/2"));
    for (e = 0; e < 5u; e++) {
        char what[80];
        ui_power_on();
        set_engine_of(TSEL, ENG[e]);
        snprintf(what, sizeof what, "engine %u: every page: k/n where SELECT has two or more, no overlap", (unsigned)ENG[e]);
        bad += pos_sweep(what, 0);
        snprintf(what, sizeof what, "engine %u: ..with REC armed, an octave pill and a message", (unsigned)ENG[e]);
        bad += pos_sweep(what, 1);
    }
    return bad;
}

static int test_section_name(void)
{
    int bad = 0;
    uint32_t f;
    ui_power_on();
    set_engine_of(TSEL, ENGI_PROPHET);
    go_title("EDIT 2");                                 /* (Sound's second page) */
    turn(EN_PRESET, 1);
    head();
    bad += check("EDIT: PRESETS to the next section: its first page (P5 OSC A)", str_eq(cur_page()->title, "P5 OSC A"));
    bad += check("..its name (OSC) where the page's name goes, no message", head_has("OSC") && !head_has("OSC A") && !ui.msg_t);
    turn(EN_SELECT, 2);
    turn(EN_PRESET, -1);
    bad += check("..PRESETS back: that section's first page (EDIT 1), not the one last used", str_eq(cur_page()->title, "EDIT 1"));
    turn(EN_PRESET, 1);
    bad += check("..forward again: OSC's first page, not the one last used", str_eq(cur_page()->title, "P5 OSC A"));
    for (f = 0; f < 40u; f++) frame();                  /* (640 ms) */
    head();
    bad += check("..after half a second the page's name again (OSC A)", head_has("OSC A"));
    go_title("EDIT 1"); turn(EN_PRESET, 1); turn(EN_SELECT, 1);
    {
        char want[16];
        sec_chip(ui.page, want, sizeof want);
        head();
        bad += check("..SELECT on from there: that page's name at once, not the section's", !head_has("OSC") && head_has(want));
    }
    set_engine_of(TSEL, ENGI_CZ);
    open_family(FAM_ENV);
    turn(EN_PRESET, 1);
    head();
    bad += check("CZ-1 ENV: PRESETS to the next envelope: WAVE 1 in the header", head_has("WAVE 1"));
    return bad;
}
static int test_no_save_in_edit(void)
{
    static const uint32_t ENG[] = {ENGI_PROPHET, ENGI_FM6};
    int bad = 0;
    for (uint32_t e = 0; e < 2u; e++) {
        uint32_t i, ok = 1;
        ui_power_on();
        set_engine_of(TSEL, ENG[e]);
        uint8_t pg[NPAGES];
        uint32_t n = nav_pages(FAM_EDIT, pg);
        for (i = 0; i < n; i++)
            if (PAGES[pg[i]].scope == SC_P5STORE || PAGES[pg[i]].graph == GR_FMSTORE) ok = 0;
        sec.ok = 0;
        sec_build();
        for (i = 0; i < sec.n; i++)
            if (str_eq(sec.name[i], "Store")) ok = 0;
        bad += check(ENG[e] == ENGI_FM6 ? "FM6: no STORE page or Store section in EDIT" :
                     "Prophet: no P5 STORE page or Store section in EDIT", ok);
    }
    return bad;
}

int main(void)
{
    int bad = 0;
#if MELODEE_BLE
    bad += test_ble_header();
    bad += test_ble_toasts();
#endif
    bad += test_pages_one_home();
    bad += test_edit_button();
    bad += test_cz_envelopes();
    bad += test_page_position();
    bad += test_section_name();
    bad += test_no_save_in_edit();
    printf(bad ? "nav: %d FAILED\n" : "nav: all ok\n", bad);
    return bad != 0;
}
