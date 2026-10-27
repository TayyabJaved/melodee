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
static uint32_t nav_owner(uint32_t i)                  /* page i's button (family) */
{
    return native_page(i, FAM_ENV) ? FAM_ENV : native_page(i, FAM_LFO) ? FAM_LFO : PAGES[i].fam;
}
static uint32_t nav_pages(uint32_t fam, uint8_t *pg)   /* the pages fam's button walks, in order -> how many */
{
    uint32_t i, n = 0;
    if (fam == FAM_ENV || fam == FAM_LFO)              /* (the engine's first: ui.c fam_pages) */
        return fam_pages(fam, pg);
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

/* PRESETS jumping a section (EDIT) or an envelope (ENV, LFO): the header shows the section's name for about half a
 * second in place of the page's (ui_sections.c sec_turn, nav_preset) */
#define NAV_SECTION_FRAMES 33u                         /* (ui_draw: a frame each 15 ms at most) */
static void nav_say_section(const char *name)
{
    char b[24];
    uint32_t i;
    for (i = 0; name[i] && i + 1u < sizeof b; i++)
        b[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
    b[i] = 0;
    ui_message(b);
    ui.msg_t = NAV_SECTION_FRAMES;
}
