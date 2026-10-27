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
