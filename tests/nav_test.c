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

/* SELECT along fam's pages from the button to the last, where it stops: every page fam's, the button lit; as many
 * back to the first, where it stops too */
static int walk(uint32_t fam, uint32_t *n_out, const char **first)
{
    uint32_t start, prev, n = 0, ok = 1;
    open_family(fam);
    start = ui.page;
    if (first) *first = cur_page()->title;
    do {
        ok &= nav_owner(ui.page) == fam && cur_fam() == fam;
        prev = ui.page;
        turn(EN_SELECT, 1);
        n++;
    } while (ui.page != prev && n < 64u);
    for (uint32_t i = 0; i < n; i++)
        turn(EN_SELECT, -1);
    *n_out = n;
    return ok && n < 64u && ui.page == start;
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

#if MELODEE_KEY_NAV
/* M3 + A#4 (the HAL's FM1_NAV_ACTION bit, key_nav_test): the note or chord at the NOTES cursor, PREVIEW off too */
static int test_hear(void)
{
    int bad = 0;
    ui_power_on();
    settings_preview = 0;
    track_defaults_steps(TSEL);
    TSEL->step[2] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 96};
    go_title("NOTES"); cursor_set(2); frame();
    audition_request = 0;
    host_pressed |= 1u << FM1_NAV_ACTION; frame();
    bad += check("M3 + A#4 on NOTES: the step's whole chord heard, PREVIEW off", audition_request && audition_count == 3u &&
                 audition_notes[0] == 60 && audition_notes[2] == 67 && audition_track == song.sel);
    audition_request = 0; cursor_set(5); frame();
    host_pressed |= 1u << FM1_NAV_ACTION; frame();
    bad += check("..an empty step: NOTHING TO PREVIEW", !audition_request && msg_is("NOTHING TO PREVIEW"));
    go_home(); frame();
    host_pressed |= 1u << FM1_NAV_ACTION; frame();
    bad += check("..away from the note pages: says where it works", !audition_request && msg_is("PREVIEW: OPEN NOTES"));
    go_title("NOTES"); cursor_set(2); song.playing = 1; frame();
    host_pressed |= 1u << FM1_NAV_ACTION; frame();
    bad += check("..playing: STOP TO PREVIEW", !audition_request && msg_is("STOP TO PREVIEW"));
    song.playing = 0;
    return bad;
}
static int test_f5_keep(void)                           /* F#5 keeps a momentary edit (the HAL's routing: key_nav_test) */
{
    int bad = 0;
    uint32_t f;
    int16_t before, *vp = 0;
    ui_power_on();
    set_engine_of(TSEL, ENGI_PROPHET);
    go_home(); frame();
    home_param(0, &vp);                                 /* (Stage's KNOB 1: the Prophet's CUTOFF) */
    before = *vp;
    frame();
    bad += check("no momentary edit: F#5 is not armed as keep (M3)", !fm1_nav.keep_on);
    btn_down(B_LFO);
    for (f = 0; f < 5u; f++) frame();
    turn(EN_K1, 3);
    frame();
    bad += check("LFO held, a knob turned: a momentary edit, F#5 armed as keep", momentary.active && fm1_nav.keep_on);
    fm1_nav.keep = 1;                                   /* (F#5 pressed: hal/fm1_input.h) */
    frame();
    bad += check("..F#5: KEPT", msg_is("KEPT") && !momentary.active && !fm1_nav.keep);
    btn_up(B_LFO);
    for (f = 0; f < 3u; f++) frame();
    bad += check("..LFO let go: the new value stays", *vp != before && !fm1_nav.keep_on);
    return bad;
}
#endif

static void sheet_hold(void)                            /* OCT+ held: the page's sheet */
{
    btn_down(B_OCTUP);
    for (uint32_t f = 0; f < 50u; f++) frame();
    btn_up(B_OCTUP); frame();
}
static void sheet_pick(const char *label)               /* the sheet open, KNOB 2 to the row, OCT+ */
{
    uint32_t i, n;
    if (pop.on != POP_SHEET) sheet_hold();
    for (i = 0; i < pop.n && !str_eq(pop.rows[i].label, label); i++)
        ;
    for (n = 0; n < 16u && pop.sel < i && pop.sel + 1u < pop.n; n++) turn(EN_K2, 1);
    press(B_OCTUP);
}
static int sheet_has(const char *label)                 /* the open sheet has that row */
{
    for (uint32_t i = 0; pop.on == POP_SHEET && i < pop.n; i++)
        if (str_eq(pop.rows[i].label, label)) return 1;
    return 0;
}
static int test_drum_grid(void)
{
    int bad = 0;
    uint32_t snare = 1;
    track_t *t;
    ui_power_on();
    stop_transport();
    set_engine_of(TSEL, ENGI_DRUM);
    t = TSEL;
    nav_drum_roll = 0;
    grid_hit(t, 0, 0, 1);                               /* programmed: kick on 1 and 5 */
    grid_hit(t, 4, 0, 1);
    t->step[8].time = ST_NOTE; t->step[8].hit = (uint8_t)(1u << snare);   /* recorded: a snare on 9 */
    t->seq_idx = 8; t->seq_pos = 0;
    bad += check("(fixture: a recorded snare hit)", recording_note(t, DRUM_LANE_NOTE[snare], 100, 8) && notes_have_recording(t));
    go_title("NOTES"); frame();
    bad += check("drum NOTES with a recording: still the grid (Grid, the default)", grid_on());
    bad += check("..programmed hits a full cell, the recorded one a short bar (its step a recorded view)",
                 (step_lanes(&t->step[0]) & 1u) && !nav_rec_cell(&t->step[0]) &&
                 (step_lanes(&t->step[8]) & (1u << snare)) && nav_rec_cell(&t->step[8]));
    sheet_hold();
    bad += check("the pattern sheet on a drum track: Drum view (Grid) its last row",
                 pop.on == POP_SHEET && sheet_has("Drum view") && str_eq(pop.rows[pop.n - 1u].label, "Drum view"));
    sheet_pick("Drum view");
    frame();
    bad += check("..Drum view to Notes: the piano roll (timing edits of recorded hits)", nav_drum_roll && !grid_on());
    if (pop.on) press(B_OCTDN);
    sheet_pick("Drum view");
    frame();
    bad += check("..again: the grid", !nav_drum_roll && grid_on());
    if (pop.on) press(B_OCTDN);
    go_home();
    set_engine_of(TSEL, ENGI_PROPHET);
    go_title("NOTES"); frame();
    sheet_hold();
    bad += check("a synth track's pattern sheet: no Drum view", pop.on == POP_SHEET && !sheet_has("Drum view"));
    if (pop.on) press(B_OCTDN);
    return bad;
}

#if MELODEE_KEY_NAV
static int lit(uint32_t id) { return led_get(fm1_led, id); }   /* (14 + k: key k) */
static int test_modifier_lights(void)
{
    int bad = 0;
    ui_power_on();
    go_title("ARP"); frame();
    ui_leds();                                          /* (its led_pos_init first, then a map of our own: the host's */
    for (uint32_t k = 0; k < 41u; k++)                  /* FM1_KEYMAP is empty; as ui_test.c test_key_lights) */
        led_pos[k] = (uint8_t)((k % FM1_NCOL) << 3 | (1u + k / FM1_NCOL));
    panel_nav(1);                                       /* (the rows, from panel.c's table) */
    fm1_nav.mod = 1u << 24; ui_leds();                  /* M1 (F5) held */
    bad += check("M1 held: F5 and its six black keys lit, no other key",
                 lit(14 + 24) && lit(14 + 13) && lit(14 + 15) && lit(14 + 17) && lit(14 + 20) && lit(14 + 22) && lit(14 + 25) &&
                 !lit(14 + 0) && !lit(14 + 26) && !lit(14 + 8));
    bad += check("..the buttons they press lit (FX .. GLO), the others dark (ARP's page light too)",
                 lit(panel.btn[B_FX]) && lit(panel.btn[B_GLO]) && lit(panel.btn[B_LFO]) && !lit(panel.btn[B_HOME]) &&
                 !lit(panel.btn[B_ARP]) && !lit(panel.btn[B_OCTUP]));
    fm1_nav.mod = 1u << 26; ui_leds();                  /* M2 (G5) */
    bad += check("M2 held: HOME .. REC lit, FX dark", lit(panel.btn[B_HOME]) && lit(panel.btn[B_REC]) && lit(panel.btn[B_ARP]) &&
                 !lit(panel.btn[B_FX]) && lit(14 + 26) && lit(14 + 13));
    fm1_nav.mod = 1u << 25; frame(); ui_leds();         /* M3 (F#5) */
    bad += check("M3 held: F#5 and A#4 (hear) lit, the buttons their usual lights (ARP's page), M1's black keys dark",
                 lit(14 + 25) && lit(14 + 17) && !lit(14 + 13) && !lit(14 + 15) && lit(panel.btn[B_ARP]) &&
                 !lit(panel.btn[B_FX]) && !lit(panel.btn[B_OCTUP]));
    bad += check("..each key as its button: A4 (ARP) lit, F3 (FX) C5 (PLAY) D#5 (OCT+) dark but glowing, E4 (the gap) dark",
                 lit(14 + 16) && !lit(14 + 0) && !lit(14 + 19) && !lit(14 + 22) && !lit(14 + 11) &&
                 !led_get(fm1_led_dim[1], 14 + 11) &&
                 led_get(fm1_led_dim[1], 14 + 0) == led_get(fm1_led_dim[1], panel.btn[B_FX]) &&
                 led_get(fm1_led_dim[1], 14 + 0) && led_get(fm1_led_dim[1], 14 + 19));
    TSEL->p[P_AMODE] = AM_SHAPE; frame(); ui_leds();
    bad += check("..ARP > SHAPE: the white keys its steps (notes), not buttons", fm1_nav.whites_off);
    TSEL->p[P_AMODE] = 0; frame();
    bad += check("..elsewhere the buttons again", !fm1_nav.whites_off);
    fm1_nav.mod = (1u << 24) | (1u << 26); ui_leds();
    bad += check("two modifiers: the usual lights (keys do nothing then)", lit(panel.btn[B_ARP]) && !lit(panel.btn[B_FX]));
    fm1_nav.mod = 0; ui_leds();
    bad += check("let go: the usual lights again (ARP's page)", lit(panel.btn[B_ARP]) && !lit(panel.btn[B_FX]) && !lit(14 + 24));
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
    bad += test_pages_one_home();
    bad += test_edit_button();
    bad += test_cz_envelopes();
    bad += test_page_position();
    bad += test_section_name();
    bad += test_no_save_in_edit();
    bad += test_drum_grid();
#if MELODEE_KEY_NAV
    bad += test_hear();
    bad += test_f5_keep();
    bad += test_modifier_lights();
#endif
    printf(bad ? "nav: %d FAILED\n" : "nav: all ok\n", bad);
    return bad != 0;
}
