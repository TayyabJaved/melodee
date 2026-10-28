/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SEQ > PATTERNS and SEQ > SONG, screens of their own drawn to the redesign's mockups (docs/design: concept_screens 3
 * and 4, ref/patterns, ref/song; the sections: mock/song_workflow_proposal.svg).
 * One look on PATTERNS, the section page and SONG: a track its numbered chip, a pattern its number in the track's
 * colour, a section its letter, a rest REST, a length bars:beats on SONG's cards and words elsewhere.
 * PATTERNS (SEQ held, or its page): the header, then a row per track: its chip (the selected one filled), a numbered
 * cell per pattern (a part's 16 in two rows of eight): the one playing filled in the track's colour (how far through it
 * at its foot), the one waiting for the bar dashed, ones holding notes raised, all three with their numbers; empty ones
 * a bare outline. At the foot the song as its columns' section letters, the column playing (else the one SONG picked) lit, a
 * section queued live dashed. KNOB k queues track k's pattern.
 * OCT+ the selected track's pattern's sheet (Copy to.., Delete pattern). Copy to: the place it goes framed in the
 * theme's colour on the track's row (the first empty one first), any knob moves it, OCT+ copies (over a pattern in
 * use: the question first), OCT- leaves.
 * SONG: its title, its length; a card a column (its section's letter and length, then "+" to add one), a track's
 * pattern in each column as a block in its colour; the column picked marked, the selected track's block in it
 * outlined; a song playing: its playhead. At the foot the column's section, its length, how often the song uses it,
 * how far the song has played. KNOB 1 the column, 2 the section it plays; OCT+ opens the section (on "+": adds what
 * plays), PLAY starts at the cursor, REC launches the column's section live (ui_input.c, ui_song.c).
 * The section page (SONG OCT+ on a column): its length and use count, PATTERNS' cells with the section's pattern
 * filled, a timeline of how each pattern loops (and is cut) in the length. KNOB k track k's pattern, SELECT the length,
 * PLAY loops it.
 * Each part remembers what it drew. Included by ui_draw.c */

static int ptc_on(void);                                /* PATTERNS' Copy to (below) */
static int dq_defer(uint32_t cf, uint32_t k, uint32_t a, uint32_t b);   /* ui_input.c: playing, at the loop end */
static uint32_t ptc_next(uint32_t from, int32_t dir);
static void ptc_row(uint32_t k);
/* the track whose four rows the page shows: the selected one's (Copy to: the four of the track it goes to) */
static uint32_t pt_track(void) { return ptc_on() ? ui.ptc_to % NTRK : song.sel % NTRK; }
static uint32_t pt_group(void) { return pt_track() / NPART * NPART; }

/* ------------------------------------------------------------ shared --- */
/* pattern b of track k holds something: a note, a hit, a recorded note's step */
static int pattern_used(uint32_t k, uint32_t b)
{
    if (b >= NPAT) return 0;
    pattern_t p;
    pattern_read(k, b, &p);
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (step_on(&p.step[i]) || (p.step[i].flags & SF_RECORDED))
            return 1;
    return 0;
}
/* pattern b of track k holds nothing at all: no note, no hit, no motion (Copy to there asks nothing) */
static int pattern_empty(uint32_t k, uint32_t b)
{
    uint32_t i;
    if (pattern_used(k, b) || pattern_sound_id(k, b))
        return 0;
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && motion_pattern[i] == b)
            return 0;
    return 1;
}
/* track k's pattern b: its steps, length and division (the one playing: the track's own; its bank lags behind) */
static void pat_timing(uint32_t k, uint32_t b, uint32_t *len, uint32_t *div)
{
    *len = (uint32_t)pattern_timing(k, b, 0);
    *div = (uint32_t)pattern_timing(k, b, 1);
}
/* its bars, whole ones up (N_DIV: a step of 1/4 .. 4BAR; steps in 4 bars) */
static uint32_t pat_bars(uint32_t k, uint32_t b)
{
    if (b == CHAIN_REST) return 1;
    static const uint8_t SPB4[] = {16, 32, 64, 128, 48, 96, 8, 4, 2, 1};
    uint32_t len, div, s;
    pat_timing(k, b, &len, &div);
    s = SPB4[div < NELEM(SPB4) ? div : 2u];
    len = len ? len : 1u;
    return (len * 4u + s - 1u) / s;
}
static uint32_t fnv1(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* ---------------------------------------------------------- PATTERNS --- */
#define PT_ROW_Y H_HEAD                                 /* a track a row: 43 px, 30 .. 201 */
#define PT_ROW_H 43
#define PT_SONG_Y (PT_ROW_Y + 4 * PT_ROW_H)             /* the song: 202 .. 239 */
/* A pattern is its number (1..16) on a cell, in its track's colour; the section page draws the same cells (sp_row).
 * A part's 16: two rows of eight (1-8 above 9-16), 19 high; a MIDI-only track's 8: one row, 39 high; 23 wide on a
 * 26 px pitch, room for two digits. */
#define PT_CELL_W 23
#define PT_CELL_X(b) (30 + 26 * (int32_t)((b) % 8u))
#define PT_CELL_Y(k, b) (trk_npat(k) > 8u ? 2 + 21 * (int32_t)((b) / 8u) : 2)
#define PT_CELL_H(k) (trk_npat(k) > 8u ? 19 : 40)
static struct { uint32_t row[NTRK], prog[NTRK], song; } ptv;

static uint32_t pt_prog_w(const track_t *t) { (void)t; return PT_CELL_W - 6u; }   /* the bar's width */
static uint32_t pt_prog(const track_t *t)               /* px through the pattern playing (of the bar), 0 stopped */
{
    uint32_t len = (uint32_t)t->p[P_SLEN];
    return song.playing && len ? (t->seq_idx % len + 1u) * 17u / len : 0u;
}
static void pt_prog_bar(int32_t x, int32_t y, uint32_t prog, uint16_t tc)   /* at the playing cell's foot */
{
    uint16_t tr = ux_mix(tc, T_BG, 35);
    cv_rrect(x, y, (int32_t)w, 2, 1, tr, tc);
    cv_rrect(x, y, prog < 2u ? 2 : (int32_t)prog, 2, 1, T_BG, tr);
}
/* track k's pattern b as a cell at x, y (h high): playing filled, holding notes raised, both with its number; empty a
 * bare outline (a Copy to place there: "+"); dst: a Copy to place (framed); next: waiting for its loop end (dashed) */
static void pt_cell(uint32_t k, uint32_t b, int32_t x, int32_t y, int32_t h, int playing, int next, int dst, uint16_t tc)
{
    char n[3];
    pat_num(n, b);
    const aafont_t *f = h < 24 ? ty_fit(TY_LB, n, PT_CELL_W - 4) : TY_LB;
    int32_t ny = ty_y(y + (playing && h < 24 ? h / 2 - 1 : h / 2), f);
    if (dst)                                            /* Copy to: the place, framed in the theme's colour */
        cv_rrect(x - 2, y - 2, PT_CELL_W + 4, h + 4, 6, T_THEME, T_BG);
    if (playing) {
        cv_rrect(x, y, PT_CELL_W, h, 4, tc, dst ? T_THEME : T_BG);
        cv_text_c(x + PT_CELL_W / 2, ny, f, n, T_INK, tc);
    } else if (pattern_used(k, b) || next) {
        cv_rrect(x, y, PT_CELL_W, h, 4, T_SURF, dst ? T_THEME : T_BG);
        cv_text_c(x + PT_CELL_W / 2, ny, f, n, pattern_used(k, b) ? tc : T_MID, T_SURF);
    } else {                                            /* empty: an outline alone */
        cv_rrect(x, y, PT_CELL_W, h, 4, dst ? T_BG : T_LINE, dst ? T_THEME : T_BG);
        cv_rrect(x + 1, y + 1, PT_CELL_W - 2, h - 2, 3, T_BG, dst ? T_BG : T_LINE);
        if (dst) cv_text_c(x + PT_CELL_W / 2, ty_y(y + h / 2, f), f, "+", T_THEME, T_BG);
    }
    if (next)                                           /* waiting for the bar: dashed */
        cv_dashed(x, y, PT_CELL_W, h, 4, tc);
}
/* the track's chip: its number, the selected one filled */
static void pt_chip(uint32_t k, int32_t h, uint16_t tc, int sel)
{
    char n[2] = {(char)('1' + k), 0};
    cv_rrect(2, 2, 24, h - 4, 6, tc, T_BG);
    if (!sel) cv_rrect(4, 4, 20, h - 8, 4, T_BG, tc);
    cv_text_c(14, ty_y(h / 2, TY_TX), TY_TX, n, sel ? T_INK : tc, sel ? tc : T_BG);
}
static void pt_row(uint32_t k, uint32_t pg)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t b, prog = pt_prog(t), sel = k == song.sel, h;
    int32_t y0 = PT_ROW_Y + (int32_t)(k % NPART) * PT_ROW_H, ch = PT_CELL_H(k);
    h = fnv1(fnv1(fnv1(2166136261u, steps_hash(t)), t->pattern_gen), t->pattern + 16u * t->pattern_next + 8192u * sel +
            16384u * (uint32_t)(t->p[P_MUTE] != 0) + 65536u * (uint32_t)t->p[P_SLEN]);
    h = fnv1(h, ux.gen * 977u + ux.pal * 31u + (uint32_t)song.playing + 2u * pg);
    if (ptc_on() && k == ui.ptc_to)                     /* Copy to: where it goes */
        h = fnv1(h, 1u + ui.ptc_src + 16u * ui.ptc_dst);
    if (!ui.force && h == ptv.row[k]) {
        if (prog != ptv.prog[k]) {                      /* playing: the bar at the cell's foot only */
            ptv.prog[k] = prog;
            cv_begin(pt_prog_w(t), 2, tc);
            pt_prog_bar(0, 0, prog, tc, pt_prog_w(t));
            cv_blit((uint32_t)(PT_CELL_X(t->pattern) + 3), (uint32_t)(y0 + PT_CELL_Y(k, t->pattern) + ch - 4));
        }
        return;
    }
    ptv.row[k] = h;
    ptv.prog[k] = prog;
    cv_begin(240, PT_ROW_H, T_BG);
    pt_chip(k, PT_ROW_H, tc, (int)sel);
    for (b = 0; b < trk_npat(k); b++) {
        int32_t x = PT_CELL_X(b), y = PT_CELL_Y(k, b);
        int dst = ptc_on() && k == ui.ptc_trk && b == ui.ptc_dst;
        pt_cell(k, b, x, y, ch, b == t->pattern, b == t->pattern_next, dst, tc);
        if (b == t->pattern && prog)
            pt_prog_bar(x + 3, y + ch - 4, prog, tc, pt_prog_w(t));
    }
    cv_blit(0, (uint32_t)y0);
}
/* the song's columns as their sections' letters (six around the column playing, else the one SONG picked: lit; a
 * section queued live: dashed) */
static void pt_song(void)
{
    uint32_t n = chain_config.count, cur, i, first, sig;
    cur = chain.running ? chain.row : live.cur < n ? live.cur : ui.song_row;
    if (n && cur >= n)
        cur = n - 1u;
    sig = fnv(2166136261u, chain_config.sect, n) + n * 131u + cur * 7919u + (uint32_t)(live.next + 1u) * 613u +
          ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == ptv.song)
        return;
    ptv.song = sig;
    for (uint32_t band = 0; band < (uint32_t)(240 - PT_SONG_Y); band += CV_MAX / (240)) {
        cv_begin_band(240,240 - PT_SONG_Y,T_BG,band);
        cv_text_on(8, ty_y(19, TY_LB), TY_LB, "SONG", T_MID, T_BG);
        if (!n) {
            cv_text_on(62, ty_y(19, TY_LB), TY_LB, "Empty", T_DIM, T_BG);
        } else {
            first = cur > 2u ? cur - 2u : 0u;
            if (n > 6u && first > n - 6u)
                first = n - 6u;
            if (n <= 6u)
                first = 0;
            for (i = first; i < first + 6u && i < n; i++) {
                int32_t x = 62 + 29 * (int32_t)(i - first);
                int on = i == cur;
                char l[2] = {(char)('A' + chain_config.sect[i]), 0};
                cv_rrect(x, 7, 26, 24, 6, on ? T_TEXT : T_SURF, T_BG);
                cv_text_c(x + 13, ty_y(19, TY_LB), TY_LB, l, on ? T_BG : T_MID, on ? T_TEXT : T_SURF);
                if (i == live.next) cv_dashed(x - 2, 5, 30, 28, 7, T_THEME);
            }
        }
        cv_blit_band(0, PT_SONG_Y);
    }
}
static void patterns_draw(void)
{
    uint32_t k;
    pt_song();
    uint32_t g = pt_group(), n = trk_n() - g < NPART ? trk_n() - g : NPART;    /* (the selected track's four) */
    for (k = 0; k < n; k++)
        pt_row(g + k, pg);
    if (ui.force && n < NPART)                          /* (the rows of tracks not in use: empty) */
        lcd_fill(0, PT_ROW_Y + (int32_t)n * PT_ROW_H, 240, (NPART - n) * PT_ROW_H, T_BG);
}
/* a pattern queued on any track (PATTERNS' OCT- clears them before it leaves) */
static int patterns_queued(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (trk[k].pattern_next < NPAT)
            return 1;
    return 0;
}
/* KNOB k: track k's next pattern (stopped: at once; playing: at the end of its bar; a song plays: refused); Copy to:
 * the place on track k's row (another row: the frame there first, the same pattern number) */
static void patgrid_edit(uint32_t k, int32_t steps)
{
    track_t *t;
    k = pt_group() + k % NPART;                         /* (KNOB k: the four shown) */
    t = &trk[k];
    if (k >= trk_n())                                   /* (no track there) */
        return;
    if (ptc_on()) {
        if (k != ui.ptc_to)
            ptc_row(k);
        else
            ui.ptc_dst = (uint8_t)ptc_next(ui.ptc_dst, steps > 0 ? 1 : -1);
        ui.force = 1;
        return;
    }
    uint32_t from = t->pattern_next < NPAT ? t->pattern_next : t->pattern;
    if (pattern_request(t, (uint32_t)clamp((int32_t)from + (steps > 0 ? 1 : -1), 0, (int32_t)trk_npat(k) - 1)))
        ui_message("STOP SONG TO SWITCH");
    else                                                /* (the page goes with it) */
        ui.pt_page = (uint8_t)(pt_focus(k) / PT_COLS);
    ui.force = 1;
}

/* ----------------------------------------------- PATTERNS: Copy to --- */
static void confirm_open(uint32_t kind, uint32_t trk);  /* ui_input.c */
static int ptc_on(void) { return ui.ptc_on && !ui.home && cur_page()->graph == GR_PATGRID && ui.ptc_trk == song.sel; }
static uint32_t ptc_next(uint32_t from, int32_t dir)    /* the next place on its row that is not the pattern copied
                                                         * (ends kept) */
{
    int32_t b = (int32_t)from + dir;
    if (ui.ptc_to == ui.ptc_trk && b == (int32_t)ui.ptc_src)
        b += dir;
    return b < 0 || b >= (int32_t)trk_npat(ui.ptc_to) ? from : (uint32_t)b;
}
/* Copy to: the place onto track k's row (KNOB k; ALGORITHM, past the four shown too), the same number (or the one
 * beside the pattern copied) */
static void ptc_row(uint32_t k)
{
    uint32_t b = ui.ptc_dst < trk_npat(k) ? ui.ptc_dst : trk_npat(k) - 1u;
    ui.ptc_to = (uint8_t)k;
    ui.ptc_dst = (uint8_t)(k == ui.ptc_trk && b == ui.ptc_src ? ptc_next(b, b ? -1 : 1) : b);
    ui.force = 1;
}
static void ptc_start(void)                             /* the pattern sheet's Copy to: the selected track's pattern */
{
    uint32_t k = song.sel % NTRK, src = trk[k].pattern, i, n = trk_npat(k), b = (src + 1u) % n;
    if (chain_busy()) {
        ui_message("STOP SONG TO COPY");
        return;
    }
    if (pattern_empty(k, src)) {
        ui_message("NOTHING TO COPY");
        return;
    }
    for (i = 1; i < n && !pattern_empty(k, (src + i) % n); i++)   /* the first empty place after it */
        ;
    if (i < n)
        b = (src + i) % n;
    ui.ptc_on = 1;
    ui.ptc_trk = ui.ptc_to = (uint8_t)k;
    ui.ptc_src = (uint8_t)src;
    ui.ptc_dst = (uint8_t)b;
    ui.force = 1;
}
static void ptc_paste(int asked)                        /* OCT+: copy it there (over one in use: asked first;
                                                         * that or the one playing, playing: at its loop end) */
{
    uint32_t k = ui.ptc_trk % NTRK, d = ui.ptc_to % NTRK;
    int rc;
    if (!asked && !pattern_empty(d, ui.ptc_dst)) {
        confirm_open(CF_PASTE_PAT, k);
        return;
    }
    if ((d != k || ui.ptc_src != ui.ptc_dst) && (ui.ptc_dst == trk[d].pattern || !pattern_empty(d, ui.ptc_dst)) &&
        dq_defer(CF_PASTE_PAT, d, ui.ptc_dst, k * NPAT + ui.ptc_src)) {
        ui.ptc_on = 0;
        return;
    }
    rc = pattern_copy_to(&trk[k], ui.ptc_src, &trk[d], ui.ptc_dst, 0);
    if (!rc)
        ui.ptc_on = 0;
    ui_message(!rc ? "PATTERN COPIED" : rc == 4 ? "PATTERN STORAGE FULL" : rc == 2 ? "PATTERN DATA FULL" : chain_busy() ? "STOP SONG TO COPY" :
               "PATTERN IS PLAYING");
    ui.force = 1;
}

/* -------------------------------------------------------------- SONG --- */
#define SG_TOP_H H_HEAD                                 /* the title: rows 0 .. 29 */
#define SG_LAB_H 38                                     /* the sections' cards (letter, length): 30 .. 67 */
#define SG_ROW_Y (SG_TOP_H + SG_LAB_H)                  /* a track a row: 32 px, 68 .. 195 */
#define SG_ROW_H 32
#define SG_FOOT_Y (SG_ROW_Y + 4 * SG_ROW_H)             /* the foot: 196 .. 239 */
#define SG_COL_W 56                                     /* a column: x 8 + 56 j, four shown */
#define SG_COLS 4u
static struct { uint32_t top, lab, row[NTRK], foot; } sgv;
static struct { uint32_t first, ncols, sel, play_col, bars, play; uint64_t total; int32_t px; } sg;   /* this frame's (bars, play: 1/384 of 4 bars) */

/* a section's length in words: "4 bars", "1 bar", "2 beats", "3 bars 2 beats"; brief, a card's: bars:beats, "3:2",
 * "4:0", "0:2" */
static void sect_len_text(char *b, uint32_t row, int brief)
{
    uint32_t beats = (sect_len_u(row) + 12u) / 24u, bars, rest;
    if (!beats) beats = 1;
    bars = beats / 4u; rest = beats % 4u;
    b[0] = 0;
    if (brief) { fmt_int(b, bars); str_cpy(b + str_len(b), ":", 2); fmt_int(b + str_len(b), rest); return; }
    if (!bars) { fmt_int(b, rest); str_cpy(b + str_len(b), rest == 1u ? " beat" : " beats", 8); return; }
    fmt_int(b, bars);
    str_cpy(b + str_len(b), bars == 1u ? " bar" : " bars", 6);
    if (rest) { str_cpy(b + str_len(b), " ", 2); fmt_int(b + str_len(b), rest); str_cpy(b + str_len(b), rest == 1u ? " beat" : " beats", 8); }
}
static void sg_time_text(char *text, uint64_t samples)
{
    uint32_t seconds = chain_div_samples(samples + FS / 2u, FS, 0);
    fmt_int(text, seconds / 60u); str_cpy(text + str_len(text), seconds % 60u < 10u ? ":0" : ":", 3);
    fmt_int(text + str_len(text), seconds % 60u);
}
/* the column playing: the song's, else the one launched live (CHAIN_ROWS: none) */
static uint32_t sg_playing(void)
{
    return chain.running && !chain.solo ? chain.row : !chain.running && live.cur < chain_config.count ? live.cur : CHAIN_ROWS;
}
/* the window (four columns around the one picked or playing), the song's length, the playhead's x (-1 none) */
static void sg_frame(void)
{
    uint32_t n = chain_config.count, r;
    sg.ncols = n < CHAIN_ROWS || se.pending == SONG_SECTIONS_MOVE ? n + 1u : CHAIN_ROWS;
    sg.sel = ui.song_row < sg.ncols ? ui.song_row : sg.ncols - 1u;
    sg.play_col = sg_playing();
    r = chain.running && !chain.solo ? chain.row : sg.sel;
    sg.first = r > 1u ? r - 1u : 0u;
    if (sg.ncols > SG_COLS && sg.first > sg.ncols - SG_COLS)
        sg.first = sg.ncols - SG_COLS;
    if (sg.ncols <= SG_COLS)
        sg.first = 0;
    for (r = 0, sg.bars = 0, sg.play = 0, sg.total = 0; r < n; r++) {
        uint32_t u = sect_len_u(r);
        sg.total += chain_row_samples(r);
        if (chain.running && !chain.solo && r < chain.row)
            sg.play += u;
        sg.bars += u;
    }
    sg.px = -1;
    if (chain.running && !chain.solo && chain.row < n && chain.len) {   /* how far through its section */
        uint32_t at = chain.rest_pos < chain.len ? chain.rest_pos : chain.len;
        if (chain.row >= sg.first && chain.row < sg.first + SG_COLS)
            sg.px = 8 + SG_COL_W * (int32_t)(chain.row - sg.first) + (int32_t)chain_div_samples((uint64_t)at * SG_COL_W, chain.len, 0);
        sg.play += chain_div_samples((uint64_t)sect_len_u(chain.row) * at, chain.len, 0);
    }
}
static void sg_lines(int32_t y, int32_t h)              /* the columns' rules, rows y .. y + h */
{
    uint32_t j;
    for (j = 0; j < SG_COLS && sg.first + j < sg.ncols; j++)
        cv_rect(8 + SG_COL_W * (int32_t)j, y, 1, h, T_LINE);
}
static void sg_head(void)
{
    if (head_shared_status() || song_edit_status()[0]) { sgv.top = ~0u; draw_head(); return; }
    char t[12];
    uint32_t secs = chain_div_samples(sg.total + FS / 2u, FS, 0);
    uint32_t sig = secs * 7u + (uint32_t)(ui.act == 4u) * 5u + ux.gen * 977u + ux.pal * 31u +
                   (ui.msg_t ? str_hash(3u, ui.msg) : 0u);
    if (!ui.force && sig == sgv.top)
        return;
    sgv.top = sig;
    cv_begin(240, SG_TOP_H, T_BG);
    cv_text_r(236, ty_y(SG_TOP_H / 2, TY_TX), TY_TX, "SONG", T_THEME, T_BG);
    if (ui.msg_t) {
        cv_free_text(8, ty_y(SG_TOP_H / 2, ty_fit(TY_TX, ui.msg, 166)), ty_fit(TY_TX, ui.msg, 166), ui.msg, T_ACCENT, T_BG,
                     166);
    } else if (chain_config.count) {                    /* its length at the tempo */
        sg_time_text(t, sg.total);
        cv_text_on(8, ty_y(SG_TOP_H / 2, TY_TX), TY_TX, t, T_TEXT, T_BG);
    }
    cv_blit(0, 0);
}
/* a card a column: its section's letter and length (picked: filled; playing: the theme's; queued live: dashed);
 * the add column: "+" */
static void sg_labels(void)
{
    uint32_t j, sig = sg.first * 7u + sg.ncols * 131u + sg.sel * 1031u + (uint32_t)(sg.px + 1) * 40503u + ux.gen * 977u +
                      ux.pal * 31u + (sg.play_col + 1u) * 613u + (uint32_t)(live.next + 1u) * 8191u;
    for (j = 0; j < chain_config.count; j++)
        sig = fnv1(fnv1(sig, chain_config.sect[j] + 32u * (uint32_t)song_range_has(j)), sect_len_u(j));
    if (!ui.force && sig == sgv.lab)
        return;
    sgv.lab = sig;
    cv_begin(240, SG_LAB_H, T_BG);
    for (j = 0; j < SG_COLS && sg.first + j < sg.ncols; j++) {
        uint32_t r = sg.first + j;
        int32_t x = 9 + SG_COL_W * (int32_t)j, w = SG_COL_W - 2;
        int sel = r == sg.sel, on = r == sg.play_col;
        if (r >= chain_config.count) {                  /* the add column */
            cv_dashed(x, 1, w, SG_LAB_H - 4, 6, sel ? T_THEME : T_LINE);
            cv_text_c(x + w / 2, ty_y(SG_LAB_H / 2 - 1, TY_TX), TY_TX, "+", sel ? T_THEME : T_DIM, T_BG);
            continue;
        }
        uint16_t fill = sel ? T_TEXT : on ? T_THEME : song_range_has(r) ? T_LIFT : T_SURF;
        uint16_t ink = sel || on ? T_BG : T_TEXT;
        char l[2] = {(char)('A' + chain_config.sect[r]), 0}, b[16];
        cv_rrect(x, 1, w, SG_LAB_H - 4, 6, fill, T_BG);
        cv_text_on(x + 4, ty_y(11, TY_LB), TY_LB, l, ink, fill);
        sect_len_text(b, r, 1);                         /* (bars:beats) */
        const aafont_t *f = ty_fit(TY_LB, b, w - 6);
        cv_text_on(x + 3, ty_y(26, f), f, b, sel || on ? T_BG : T_MID, fill);
        if (r == live.next) cv_dashed(x - 1, 0, w + 2, SG_LAB_H - 2, 7, T_THEME);
    }
    cv_blit(0, SG_TOP_H);
}
/* Paint one vertical time range through the block's fill. Reuse its exact
 * radius-five coverage, including the anti-aliased corners and selection rim. */
static void sg_punch_span(int32_t x, int32_t w, int32_t from, int32_t to, uint16_t c, uint16_t under)
{
    if (from < x) from = x;
    if (from >= x + w) from = x + w - 1;
    if (to > x + w) to = x + w;
    if (to <= from) to = from + 1;
    cv_rect(from, 7, to - from, 18, c);
    for (int32_t px = from; px < to; px++) {
        int32_t edge = px - x < x + w - 1 - px ? px - x : x + w - 1 - px;
        for (int32_t y = 0; y < 5; y++) {
            uint32_t a = edge < 5 ? rr_cov(5, edge, y) : 16u;
            if (!a) continue;
            uint16_t fill = a == 16u ? c : ux_mix(under, c, (int32_t)(a * 100u / 16u));
            cv_pset(px, 2 + y, fill);
            cv_pset(px, 29 - y, fill);
        }
    }
}
/* the column's punches, each loop of track 1's pattern (resting: each bar) across its section */
static void sg_punch_block(uint32_t r, int32_t x, int32_t w, uint16_t under)
{
    uint32_t steps = song_punch_steps_at(r), drawn = 0, bank = chain_patterns[r][0];
    uint32_t loop = bank < NPAT ? sect_pattern_u(0, bank) : 96u, total = sect_len_u(r);
    uint32_t loops = loop ? (total + loop - 1u) / loop : 1u;
    if (!loops) loops = 1;
    for (uint32_t pass = 0; pass < CHAIN_FX_EVENTS; pass++) {
        int32_t next = -1;
        for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) {
            const chain_fx_t *fx = &chain_config.fx[r][j];
            if ((drawn & (1u << j)) || !fx->fx || fx->start >= steps) continue;
            if (next < 0 || fx->start < chain_config.fx[r][next].start) next = (int32_t)j;
        }
        if (next < 0) break;
        drawn |= 1u << next;
        const chain_fx_t *fx = &chain_config.fx[r][next];
        uint32_t end = fx->start + fx->length;
        if (end > steps) end = steps;
        uint16_t c = ux_mix(punch_color(fx->fx - 1u), T_TEXT, 30);
        for (uint32_t rep = 0; rep < loops; rep++) {
            uint32_t a = loop * rep + loop * fx->start / steps;
            uint32_t b = loop * rep + loop * end / steps;
            if (a >= total) break;
            if (b > total) b = total;
            int32_t from = x + (int32_t)(a * (uint32_t)w / total), to = x + (int32_t)(b * (uint32_t)w / total);
            sg_punch_span(x, w, from, to > from ? to : from + 1, c, under);
        }
    }
}
/* track k's blocks: a column its pattern (a rest: "--"), the picked column's outlined */
static void sg_row(uint32_t k)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t live_trk = k < trk_n(), cols = live_trk ? SG_COLS : 0u;
    uint32_t j, h = fnv1(2166136261u, sg.first + 16u * sg.ncols + 256u * sg.sel + 4096u * (k == song.sel) +
                                     65536u * (uint32_t)(t->p[P_MUTE] != 0) + 131072u * live_trk);
    h = fnv1(fnv1(h, (uint32_t)(sg.px + 1)), ux.gen * 977u + ux.pal * 31u + t->pattern_gen);
    for (j = 0; j < cols && sg.first + j < chain_config.count; j++) {
        uint32_t r = sg.first + j, p = chain_patterns[r][k];
        h = fnv1(h, p + 256u * (uint32_t)pattern_used(k, p));
        h = fnv(h, chain_config.fx[r], sizeof chain_config.fx[r]);
        h = fnv1(h, song_punch_steps_at(r) + 128u * sect_len_u(r));
    }
    if (!ui.force && h == sgv.row[k])
        return;
    sgv.row[k] = h;
    cv_begin(240, SG_ROW_H, T_BG);
    sg_lines(0, SG_ROW_H);
    for (j = 0; j < cols && sg.first + j < chain_config.count; j++) {
        uint32_t r = sg.first + j, p = chain_patterns[r][k];
        int pick = r == sg.sel && k == song.sel;
        int used = pattern_used(k, p);
        int32_t x = 9 + SG_COL_W * (int32_t)j, w = SG_COL_W - 2;
        char l[5];                                      /* (a pattern: its number, as PATTERNS shows it) */
        if (p >= NPAT) str_cpy(l, "REST", sizeof l);
        else pat_num(l, p);
        if (used) {
            if (pick)
                cv_rrect(x - 1, 1, w + 2, 30, 6, T_TEXT, T_BG);
            cv_rrect(x, 2, w, 28, 5, tc, pick ? T_TEXT : T_BG);
            sg_punch_block(r, x, w, pick ? T_TEXT : T_BG);
            cv_over = 1;
            cv_text_on(x + 6, ty_y(16, TY_LB), TY_LB, l, T_INK, tc);
            cv_over = 0;
        } else {
            if (p < NPAT || pick) cv_dashed(x, 2, w, 28, 5, tc);
            cv_text_on(x + 6, ty_y(16, TY_LB), TY_LB, l, p < NPAT ? tc : pick ? T_MID : T_DIM, T_BG);
        }
    }
    if (sg.px >= 0)
        cv_rect(sg.px, 0, 2, SG_ROW_H, T_TEXT);
    cv_blit(0, (uint32_t)(SG_ROW_Y + (int32_t)(k % NPART) * SG_ROW_H));
}
static void sg_foot(void)                               /* the column picked: its section, what OCT+ does; how far */
{
    char b[40], l[20];
    uint32_t n = chain_config.count, r = sg.sel;
    uint32_t sig = n * 7u + (uint32_t)sg.bars * 131u + (uint32_t)sg.play * 1031u + r * 40503u + (uint32_t)(sg.px + 1) * 613u +
                   ux.gen * 977u + ux.pal * 31u + sg.first * 17u + sg.ncols + (r < n ? chain_config.sect[r] * 7919u + sect_len_u(r) : 0u) +
                   (uint32_t)song.playing * 3u + (uint32_t)chain_busy() * 5u;
    if (!ui.force && sig == sgv.foot)
        return;
    sgv.foot = sig;
    cv_begin(240, 240 - SG_FOOT_Y, T_BG);
    sg_lines(0, 4);
    if (sg.px >= 0)
        cv_rect(sg.px, 0, 2, 6, T_TEXT);
    if (r >= n) {                                       /* the add column: what OCT+ adds */
        cv_text_on(8, ty_y(17, TY_LB), TY_LB, "OCT+ adds what plays now", T_MID, T_BG);
    } else {
        str_cpy(b, "A \xb7 ", sizeof b); b[0] = (char)('A' + chain_config.sect[r]);
        sect_len_text(l, r, 0); str_cpy(b + str_len(b), l, sizeof b - str_len(b));
        if (!chain_config.len[r]) str_cpy(b + str_len(b), " AUTO", sizeof b - str_len(b));
        const aafont_t *f = ty_fit(TY_TX, b, 150);
        cv_text_on(8, ty_y(17, f), f, b, T_TEXT, T_BG);
        uint32_t uses = sect_uses(chain_config.sect[r]);
        str_cpy(l, "used ", sizeof l); fmt_int(l + 5, uses); str_cpy(l + str_len(l), "x", 2);
        cv_text_r(232, ty_y(17, TY_LB), TY_LB, l, T_MID, T_BG);
    }
    cv_rrect(8, 32, 224, 6, 3, T_SURF, T_BG);
    if (chain.running && !chain.solo && sg.bars) {
        int32_t w = (int32_t)(sg.play * 224u / sg.bars);
        cv_rrect(8, 32, w < 6 ? 6 : w, 6, 3, T_TEXT, T_SURF);
    }
    cv_blit(0, SG_FOOT_Y);
}

/* ------------------------------------------------------- SONG > section --- */
/* The section page: its length on top (how often the song plays it at the right), PATTERNS' cells for each track
 * (the section's pattern filled, a rest said across them), then how each pattern fills the length (loops: lighter
 * after the first; cut: a red edge). */
#define SP_LEN_Y H_HEAD                                 /* the length: 30 .. 53 */
#define SP_ROW_Y 54                                     /* a track a row: 36 px, 54 .. 197 */
#define SP_ROW_H 36
#define SP_CELL_Y(k, b) (trk_npat(k) > 8u ? 1 + 18 * (int32_t)((b) / 8u) : 1)   /* (PATTERNS' cells, 16 high) */
#define SP_CELL_H(k) (trk_npat(k) > 8u ? 16 : 34)
#define SP_TL_Y (SP_ROW_Y + 4 * SP_ROW_H)               /* the timeline, a lane a track: 198 .. 239 */
#define SP_TL_H 42
#define SP_LANE_Y 6                                     /* (its lanes: 8 px apart) */
static struct { uint32_t len, row[NTRK], tl; } sgp;
static void sp_len(void)
{
    char b[24];
    uint32_t row = sp.row, uses = sect_uses(chain_config.sect[row]);
    uint32_t sig = sect_len_u(row) * 7u + chain_config.len[row] * 131u + uses * 8191u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == sgp.len)
        return;
    sgp.len = sig;
    cv_begin(240, SP_ROW_Y - SP_LEN_Y, T_BG);
    cv_rrect(4, 1, 232, 22, 5, T_SURF, T_BG);
    cv_text_on(10, ty_y(12, TY_LB), TY_LB, "LENGTH", T_MID, T_SURF);
    sect_len_text(b, row, 0);
    const aafont_t *f = ty_fit(TY_TX, b, 90);
    int32_t x = cv_text_on(80, ty_y(12, f), f, b, T_TEXT, T_SURF);
    if (!chain_config.len[row]) {                       /* (its longest pattern's: AUTO) */
        cv_rrect(x + 5, 4, 40, 16, 5, T_THEME, T_SURF);
        cv_text_c(x + 25, ty_y(12, TY_LB), TY_LB, "AUTO", T_BG, T_THEME);
    }
    fmt_int(b, uses); str_cpy(b + str_len(b), "x", 2);
    cv_text_r(230, ty_y(12, TY_LB), TY_LB, b, T_MID, T_SURF);
    cv_blit(0, SP_LEN_Y);
}
static void sp_row(uint32_t k)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t row = sp.row, bank = chain_patterns[row][k], b, n = trk_npat(k);
    int32_t y0 = SP_ROW_Y + (int32_t)(k % NPART) * SP_ROW_H;
    uint32_t h = fnv1(fnv1(fnv1(2166136261u, steps_hash(t)), t->pattern_gen), bank + 256u * (k == song.sel) +
                      4096u * (uint32_t)(t->p[P_MUTE] != 0) + 65536u * (k < trk_n()));
    h = fnv1(h, ux.gen * 977u + ux.pal * 31u);
    if (!ui.force && h == sgp.row[k])
        return;
    sgp.row[k] = h;
    cv_begin(240, SP_ROW_H, T_BG);
    if (k < trk_n()) {
        pt_chip(k, SP_ROW_H, tc, k == song.sel);
        if (bank >= NPAT) {                             /* a rest: said in words where its cells would be */
            int32_t x0 = PT_CELL_X(0), x1 = PT_CELL_X(7u) + PT_CELL_W;
            cv_rrect(x0, 1, x1 - x0, SP_ROW_H - 2, 4, T_SURF, T_BG);
            cv_text_c((x0 + x1) / 2, ty_y(SP_ROW_H / 2, TY_LB), TY_LB, "REST", T_MID, T_SURF);
        } else for (b = 0; b < n; b++)
            pt_cell(k, b, PT_CELL_X(b), SP_CELL_Y(k, b), SP_CELL_H(k), b == bank, 0, 0, tc);
    }
    cv_blit(0, (uint32_t)y0);
}
static void sp_timeline(void)
{
    uint32_t row = sp.row, total = sect_len_u(row), k, g = trk_group();
    int32_t play = -1;
    if (sect_looping() && chain.config.sect[chain.row] == chain_config.sect[row] && chain.len)
        play = 30 + (int32_t)chain_div_samples((uint64_t)(chain.rest_pos < chain.len ? chain.rest_pos : chain.len) * 202u, chain.len, 0);
    uint32_t sig = total * 7u + (uint32_t)(play + 1) * 131u + ux.gen * 977u + ux.pal * 31u;
    for (k = g; k < g + NPART && k < NTRK; k++) {
        uint32_t bank = chain_patterns[row][k];
        sig = fnv1(sig, bank + 64u * (bank < NPAT ? sect_pattern_u(k, bank) : 0u) + 7u * (k < trk_n()));
    }
    if (!ui.force && sig == sgp.tl)
        return;
    sgp.tl = sig;
    cv_begin(240, SP_TL_H, T_BG);
    uint32_t bars = (total + 95u) / 96u;               /* a rule a bar (at most sixteen) */
    uint32_t stride = bars <= 16u ? 1u : bars <= 32u ? 2u : bars <= 64u ? 4u : 8u;
    for (uint32_t bar = 0; bar < bars; bar += stride)
        cv_rect(30 + (int32_t)(bar * 96u * 202u / total), SP_LANE_Y - 3, 1, 4 * 8 + 4, T_LINE);
    for (k = g; k < g + NPART && k < NTRK; k++) {
        int32_t y = SP_LANE_Y + (int32_t)(k - g) * 8;
        if (k >= trk_n()) continue;
        uint32_t bank = chain_patterns[row][k];
        uint16_t tc = trk[k].p[P_MUTE] ? T_DIM : T_TRK(k);
        if (bank >= NPAT) {                             /* a rest: a dotted line */
            for (int32_t x = 30; x < 232; x += 5) cv_rect(x, y + 3, 2, 1, T_DIM);
            continue;
        }
        uint32_t loop = sect_pattern_u(k, bank), at = 0;
        if (!loop) continue;
        for (uint32_t i = 0; at < total; i++, at += loop) {
            uint32_t end = at + loop < total ? at + loop : total;
            int32_t x0 = 30 + (int32_t)(at * 202u / total), x1 = 30 + (int32_t)(end * 202u / total);
            uint16_t c = i ? ux_mix(T_BG, tc, 55) : tc;
            cv_rrect(x0 + 1, y, x1 - x0 - 2 > 2 ? x1 - x0 - 2 : 2, 6, 2, c, T_BG);
            if (end < at + loop) cv_rect(x1 - 2, y - 1, 2, 8, T_REC);   /* cut where the section ends */
        }
    }
    if (play >= 0) cv_rect(play, SP_LANE_Y - 3, 2, 4 * 8 + 4, T_TEXT);
    cv_blit(0, SP_TL_Y);
}
static void sect_draw(void)
{
    uint32_t k;
    draw_head();
    sp_len();
    for (k = trk_group(); k < trk_group() + NPART; k++)
        sp_row(k);
    sp_timeline();
}
static void song_draw(void)
{
    uint32_t k;
    if (sect_on()) { sect_draw(); return; }
    sg_frame();
    sg_head();
    sg_foot();
    sg_labels();
    for (k = trk_group(); k < trk_group() + NPART; k++)   /* (the selected track's four) */
        sg_row(k);
}
