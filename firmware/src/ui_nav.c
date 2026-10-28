/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Header and navigation (task.md): Bluetooth changes as brief toasts, its status in the header only on the Bluetooth
 * page (ui_draw.c ble_head_text). Included at the end of ui_input.c */

#if MELODEE_BLE
static struct { uint8_t ready, failed; } nav_ble __attribute__((section(".pool")));
/* main loop, after mel_ble_poll: connected, disconnected, a failure; the Bluetooth page shows them itself */
static void nav_ble_toasts(void)
{
    uint8_t ready = (uint8_t)(mb.on && mb.ready), failed = (uint8_t)(mb.failure[0] != 0);
    if (ui.menu != 3u) {
        if (ready != nav_ble.ready) ui_message(ready ? "BLE CONNECTED" : "BLE DISCONNECTED");
        else if (failed && !nav_ble.failed) ui_message(mb.failure);
    }
    nav_ble.ready = ready;
    nav_ble.failed = failed;
}
#endif

static void nav_say_section(const char *name);

/* ENV, LFO and EDIT: one home per page (task.md 2). An engine's own envelope and LFO pages (ui.c native_titles:
 * Prophet, FM6, CZ-1) are ENV's and LFO's only: SELECT walks the lit button's pages, EDIT's sections leave them out,
 * EDIT pressed there goes to EDIT, PRESETS jumps an envelope (CZ-1's: five pages each) */
/* EDIT has no save page: the Prophet's and FM6's STORE pages (SLOT, STORE, SEND, INIT) are no button's, so SELECT,
 * the sections and the map never show them; SAVE saves every engine's sound, the sound sheet has Init sound */
static int nav_hidden(uint32_t i)
{
    return PAGES[i].scope == SC_P5STORE || PAGES[i].graph == GR_FMSTORE;
}
static uint32_t nav_owner(uint32_t i)                  /* page i's button (family); FAM_COUNT: none */
{
    return nav_hidden(i) ? FAM_COUNT : native_page(i, FAM_ENV) ? FAM_ENV : native_page(i, FAM_LFO) ? FAM_LFO :
           PAGES[i].fam == FAM_TRK ? FAM_GLO : PAGES[i].fam;   /* (MIXER is GLO's: one list with the global pages) */
}
static uint32_t nav_pages(uint32_t fam, uint8_t *pg)   /* the pages fam's button walks, in order -> how many */
{
    uint32_t i, n = 0;
    if (fam == FAM_ENV || fam == FAM_LFO)              /* (the engine's first: ui.c fam_pages) */
        return fam_pages(fam, pg);
    if (fam == FAM_GLO) {                              /* (MIXER first, then GLOBAL ..; GLO's main page before them) */
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == FAM_TRK && page_visible(i))
                pg[n++] = (uint8_t)i;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == FAM_GLO && page_visible(i))
                pg[n++] = (uint8_t)i;
        return n;
    }
    for (i = 0; i < NPAGES; i++)
        if (nav_owner(i) == fam && page_visible(i))
            pg[n++] = (uint8_t)i;
    return n;
}
static uint32_t nav_group(uint32_t fam, uint32_t i)    /* the engine's envelope / LFO page i is in (+1), 0 none */
{
    const char *const *t = native_titles(fam);
    uint32_t g;
    for (g = 0; t && t[g]; g++)
        if (str_prefix(PAGES[i].title, t[g]))
            return g + 1u;
    return 0;
}
static uint8_t nav_next_group(uint32_t fam, const uint8_t *pg, uint32_t n, uint32_t k, int32_t dir)
{                                                      /* from pg[k]: the first page of the next / previous group */
    uint32_t g = nav_group(fam, pg[k]), j = k;
    do j = (j + (dir > 0 ? 1u : n - 1u)) % n;
    while (j != k && g && nav_group(fam, pg[j]) == g);
    if (dir < 0)                                       /* (back: to that group's first page) */
        for (g = nav_group(fam, pg[j]); g && nav_group(fam, pg[(j + n - 1u) % n]) == g && (j + n - 1u) % n != k;)
            j = (j + n - 1u) % n;
    return pg[j];
}
static void nav_scroll(int32_t dir)                    /* SELECT: the lit button's pages, both ways (wraps) */
{
    uint8_t pg[NPAGES];
    uint32_t fam = nav_owner(ui.page), n = nav_pages(fam, pg), k;
    for (k = 0; k < n && pg[k] != ui.page; k++)
        ;
    if (!n || (n == 1u && k == 0u))
        return;
    ui.page = k == n ? pg[0] : pg[(k + (dir > 0 ? 1u : n - 1u)) % n];
    ui.fam_last[fam] = ui.page;
    page_entered();
}
static int nav_preset(int32_t s)                       /* PRESETS on an engine's envelope / LFO page: the next group */
{
    uint8_t pg[NPAGES];
    uint32_t fam, n, k;
    if (ui.home || cur_page()->scope == SC_FMOP)        /* (FM6's EG pages: PRESETS picks the operator) */
        return 0;
    fam = nav_owner(ui.page);
    if ((fam != FAM_ENV && fam != FAM_LFO) || !native_titles(fam))
        return 0;
    n = nav_pages(fam, pg);
    for (k = 0; k < n && pg[k] != ui.page; k++)
        ;
    if (k == n || n < 2u)
        return 1;
    ui.page = nav_next_group(fam, pg, n, k, s);
    ui.fam_last[fam] = ui.page;
    page_entered();
    if (nav_group(fam, ui.page))                        /* ("Pitch 1", "Wave 1", "Wheel"; a track page: its name) */
        nav_say_section(sec_rule(ui.page, 0));
    return 1;
}
/* the header's name for a page: the track LFO's say whose they are (TRACK LFO, TRACK LFO 2, TRACK DEST), next to
 * an engine's own (P5 LFO, FM LFO, CZ VIBRATO) */
static void nav_chip(const page_t *pg, char *b)
{
    char t[16];
    if (cseq_page()) {                                  /* SEQ > CHORDS: the step too (ui_chords.c) */
        cseq_chip(b);
        return;
    }
    if (pg->scope != SC_TRACK || pg->fam != FAM_LFO || pg->graph == GR_MOD)
        return;
    str_cpy(t, str_eq(b, "LFO DEST") ? "DEST" : b, sizeof t);
    str_cpy(b, "TRACK ", 16);
    str_cpy(b + 6, t, 10);
}

/* The page position (task.md 1): "k/n" in the label face right after the BPM and the REC mark, on every page of a
 * button with two or more. It counts what SELECT walks (an EDIT section's pages, as its tabs show).
 * The right side's octave pill and song row give way first, then the page name's face, then the name is ellipsised;
 * a status (ERASING, WAIT LOOP, BLE, a PIN) keeps its room: only then does the position step aside */
static uint32_t nav_pos(uint32_t *k)                   /* -> n, the page's place in *k */
{
    uint8_t pg[NPAGES];
    uint32_t n = 0, m = 0, i, fam;
    *k = 0;
    if (song_punch_on())
        return 0;
    if (ui.home) {                                     /* (HOME: its "1/2" where there is room, after its octave
        *k = ui.home_pg;                               * pill: ui_draw.c draw_head) */
        return 2u;
    }
    if (designer_on() && !name_on()) {                 /* the designer: its stage's pages */
        designer_pages(&n, k);
        return n;
    }
    fam = nav_owner(ui.page);
    n = nav_pages(fam, pg);
    if (fam == FAM_EDIT && sec_on()) {                 /* an engine in sections: the section's pages */
        uint32_t sc = sec_of(ui.page);
        for (i = 0; i < n; i++)
            if (PAGES[pg[i]].scope == SC_FMOP ? PAGES[ui.page].scope == SC_FMOP : sec_of(pg[i]) == sc)
                pg[m++] = pg[i];
        n = m;
    }
    for (i = 0; i < n && pg[i] != ui.page; i++)
        ;
    if (fam == FAM_GLO && i < n)                       /* (GLO's main page is the first: MIXER 2/6) */
        return *k = i + 1u, n + 1u;
    *k = i;
    return i < n ? n : 0u;
}
static int32_t nav_head_pos(int32_t x)                 /* (ui_draw.c draw_head) -> the pen after it */
{
    char b[12], bt[24];
    const char *st = "";
    uint32_t k, n = nav_pos(&k);
    int32_t w;
    if (n < 2u || ui.home)
        return x;
    fmt_int(b, (int32_t)k + 1);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), (int32_t)n);
    w = text_w(TY_LB, b);
    if (seq_erase_active(TSEL))
        st = "ERASING";
    else if (head_waiting())
        st = "WAIT LOOP";
#if MELODEE_BLE
    else {
        ble_head_text(bt);
        st = bt;
    }
#endif
    if (st[0] && x + 6 + w + 6 + text_w(TY_LB, st) > 236)
        return x;
    cv_text_on(x + 6, ty_y(H_HEAD / 2, TY_LB), TY_LB, b, T_MID, T_BG);
    return x + 6 + w;
}

/* PRESETS jumping a section (EDIT) or an envelope (ENV, LFO): the header shows the section's name, in capitals, in
 * place of the page's name for half a second, while that page stays (ui_sections.c sec_turn, nav_preset) */
#define NAV_SECTION_MS 500u
static struct { const char *name; uint32_t until; uint8_t page; } nav_sec __attribute__((section(".pool")));
static void nav_say_section(const char *name)
{
    nav_sec.name = name;
    nav_sec.page = ui.page;
    nav_sec.until = (fm1_ms + NAV_SECTION_MS) | 1u;    /* (0: none) */
}
static void nav_head_chip(char *chip)                  /* (ui_draw.c draw_head, after head_chip_text) */
{
    uint32_t i;
    if (!nav_sec.until)
        return;
    if (ui.home || ui.page != nav_sec.page || (int32_t)(nav_sec.until - fm1_ms) <= 0) {
        nav_sec.until = 0;
        return;
    }
    for (i = 0; nav_sec.name[i] && i < 15u; i++)
        chip[i] = nav_sec.name[i] >= 'a' && nav_sec.name[i] <= 'z' ? (char)(nav_sec.name[i] - 32) : nav_sec.name[i];
    chip[i] = 0;
}


/* M1 / M2 / M3 (MELODEE_KEY_NAV): F#5 keeps a momentary edit (LFO held, a knob turned), as OCT+ does; the HAL routes it
 * so while one is on (hal/fm1_input.h fm1_input_nav_keep) */
static void nav_keep(void)                             /* (ui_input.c, each pass: by OCT+'s KEPT) */
{
#if MELODEE_KEY_NAV
    fm1_input_nav_keep(momentary.active);
    fm1_input_nav_whites_on(!shape_page_on());          /* (M3's white keys: ARP > SHAPE's steps there, ui.c) */
    if (fm1_nav.keep) {
        fm1_nav.keep = 0;
        if (momentary.active) {
            momentary.active = momentary.count = momentary.native = 0;
            ui_message("KEPT");
        }
    }
#endif
}

/* Drum tracks: NOTES is the grid, programmed and recorded hits together (task: option A). Upstream switched to the
 * piano roll once a pattern held a recording, where programmed hits do not show. A recorded hit (its step a view of
 * timed notes, SF_RECORDED) is a short bar, a programmed one a full cell. Editing a recorded cell on the grid makes that
 * step programmed (ui.c grid_hit); a hit's pitch, length and sound: DRUM HIT. The pattern sheet's "Drum view" (drum
 * tracks only, its last row) brings the roll back for timing edits: Notes */
static uint8_t nav_drum_roll __attribute__((section(".pool")));   /* 0 Grid (the power-on default), 1 Notes */
static int nav_drum_notes(void) { return nav_drum_roll; }      /* (ui.c grid_on) */
static int nav_rec_cell(const step_t *st) { return (st->flags & SF_RECORDED) != 0; }   /* (the grids' cells) */
static void nav_dview_v(char *b) { str_cpy(b, nav_drum_roll ? "Notes" : "Grid", 8); }
static void nav_dview_turn(int32_t s) { nav_drum_roll = (uint8_t)(s > 0); ui.force = 1; }
static void nav_dview(void) { nav_dview_turn(!nav_drum_roll); }   /* (OCT+: the other view) */

/* M1 / M2 / M3 held (MELODEE_KEY_NAV): the keys light their map, the modifier and its black keys, and the panel buttons
 * light the ones those keys press (M1: FX .. GLO, M2: HOME .. REC), in place of the usual lights, until it is let go
 * (ui_input.c ui_leds). M3 presses them all (its white keys) and OCT- OCT+: the buttons keep their lights and each key
 * shows its button's (nav_mirror), A#4 lit for hearing the cursor's note (no button) */
static void nav_leds(uint8_t *nl, uint32_t *keys, uint32_t *play)
{
#if MELODEE_KEY_NAV
    uint32_t mod = fm1_nav.mod, r, i, b;
    if (!mod || (mod & (mod - 1u)))                     /* (none, or two: keys do nothing) */
        return;
    for (r = 0; r < FM1_NAV_ROWS && mod != 1u << FM1_NAV_MOD[r]; r++)
        ;
    if (r == FM1_NAV_ROWS)
        return;
    if (r == 2u) {                                      /* M3: the modifier, A#4; the rest nav_mirror's */
        *keys = mod;
        *play = 0;
        for (i = 0; i < FM1_NAV_N; i++)
            if (fm1_nav.row[r][i] == FM1_NAV_ACTION)
                *keys |= 1u << FM1_NAV_KEY[i];
        return;
    }
    for (b = 0; b < 14u; b++)                           /* (led_put only lights: the button plane cleared) */
        if (led_pos[b] != 0xFFu)
            nl[led_pos[b] >> 3] &= (uint8_t)~(1u << (led_pos[b] & 7u));
    *keys = mod;
    *play = 0;
    for (i = 0; i < FM1_NAV_N; i++)
        if (fm1_nav.row[r][i] <= FM1_NAV_ACTION) {
            *keys |= 1u << FM1_NAV_KEY[i];
            if (fm1_nav.row[r][i] < FM1_NAV_ACTION)
                led_put(nl, fm1_nav.row[r][i], 1);
        }
#else
    (void)nl; (void)keys; (void)play;
#endif
}
/* M3 held: each key that presses a button lit as that button is, plane by plane (pl: lit, dim, glow, breathing), so
 * key and button match; one whose button is dark (LIGHTS OFF, idle) glows to show it is there */
static void nav_mirror(uint8_t *const pl[4])
{
#if MELODEE_KEY_NAV
    uint32_t i, p, b, k, q, on;
    if (fm1_nav.mod != 1u << FM1_NAV_MOD[2])
        return;
    for (i = 0; i < FM1_NAV_WHITES + FM1_NAV_N; i++) {
        k = i < FM1_NAV_WHITES ? FM1_NAV_WHITE[i] : FM1_NAV_KEY[i - FM1_NAV_WHITES];
        b = i >= FM1_NAV_WHITES ? fm1_nav.row[2][i - FM1_NAV_WHITES] :
            fm1_nav.whites && !fm1_nav.whites_off ? fm1_nav.white[i] : 0xFFu;
        if (b >= FM1_NAV_ACTION || (q = led_pos[14u + k]) == 0xFFu)
            continue;
        for (p = 0, on = 0; p < 4u; p++) {
            pl[p][q >> 3] &= (uint8_t)~(1u << (q & 7u));
            led_put(pl[p], 14u + k, led_get(pl[p], b));
            on |= (uint32_t)led_get(pl[p], b);
        }
        led_put(pl[2], 14u + k, !on);
    }
#else
    (void)pl;
#endif
}
