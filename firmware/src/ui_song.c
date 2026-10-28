/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SONG edits columns of sections (song_chain.c): which section a column plays, the columns' order, and on the section
 * page what a section is (each track's pattern, its length, sounds, punches) in every column of it at once.
 * Arrangement transactions retain pattern references, never copy note banks.
 * Eight demand-allocated snapshots support the existing EDIT undo gestures. */
#define SONG_UNDO_LEVELS 8u
typedef struct {
    chain_config_t config;
    uint8_t patterns[CHAIN_ROWS][NTRK], cursor;
} song_edit_snapshot_t;
static struct {
    uint32_t signature, guard;
    uint16_t epoch;
    uint8_t top, count, range, anchor, end, pending, first, last, track, locked;
} se;
enum { SONG_SECTIONS_COPY = 1, SONG_SECTIONS_MOVE };
/* the section page: open over SONG, the column it shows (its section is edited in every column of it) */
static struct { uint8_t open, row; } sp;
static int sect_on(void)
{
    return sp.open && !ui.home && cur_page()->graph == GR_SONG && sp.row < chain_config.count;
}
static int sect_looping(void) { return chain.running && chain.solo; }
/* The target runtime exposes memcpy, so handle overlapping editor ranges here. */
static void song_edit_shift(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    if ((uintptr_t)d < (uintptr_t)s) for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    else while (n) { n--; d[n] = s[n]; }
}
static uint32_t song_edit_signature(void)
{
    return fnv(fnv(2166136261u, &chain_config, sizeof chain_config), chain_patterns, sizeof chain_patterns);
}
static void song_edit_cancel(void)
{
    se.pending = se.range = 0;
    if (ui.song_row >= CHAIN_ROWS) ui.song_row = CHAIN_ROWS - 1u;
    ui.force = 1;
}
static void song_edit_sync(void)
{
    uint32_t f = motion_guard();
    uint32_t sig = song_edit_signature();
    if (se.epoch != pattern_epoch || se.signature != sig) {
        resource_release(RES_SONG_UNDO);
        memset(&se, 0, sizeof se);
        se.epoch = pattern_epoch; se.signature = sig;
    }
    if (!resource[RES_SONG_UNDO].size) se.top = se.count = 0;
    motion_unguard(f);
}
static void song_snapshot_get(song_edit_snapshot_t *s)
{
    s->config = chain_config;
    memcpy(s->patterns, chain_patterns, sizeof s->patterns);
    s->cursor = ui.song_row;
}
/* stopped; or the section page looping its section (its patterns and length change as it plays) */
static int song_edit_begin(void)
{
    int busy = chain_busy() && !(sect_on() && sect_looping());
    if (busy) { ui_message("STOP TO EDIT"); return 0; }
    uint32_t f = motion_guard();
    if (chain_busy() && !(sect_on() && sect_looping())) { motion_unguard(f); ui_message("STOP TO EDIT"); return 0; }
    song_edit_sync();
    uint32_t n = se.top < SONG_UNDO_LEVELS ? se.top + 1u : SONG_UNDO_LEVELS;
    song_edit_snapshot_t *h = resource_resize(RES_SONG_UNDO, n * sizeof *h);
    if (!h) { motion_unguard(f); ui_message("NO EDIT MEMORY"); return 0; }
    if (se.top == SONG_UNDO_LEVELS) {
        song_edit_shift(h, h + 1, (SONG_UNDO_LEVELS - 1u) * sizeof *h); se.top--;
    }
    song_snapshot_get(&h[se.top++]); se.count = se.top;
    se.guard = f; se.locked = 1; return 1;
}
static void song_edit_end(void)
{
    if (sect_looping()) {                               /* (the loop plays the new length from its next pass) */
        memcpy(chain.config.len, chain_config.len, sizeof chain.config.len);
        chain.len_dirty = 1;
    }
    se.signature = song_edit_signature(); ui.force = 1;
    if (se.locked) { se.locked = 0; motion_unguard(se.guard); }
}
static void song_loop_set(int on)
{
    if (chain_config.rsv[CHAIN_LOOP] == (uint32_t)on || !song_edit_begin()) return;
    chain_config.rsv[CHAIN_LOOP] = (uint8_t)on;
    song_edit_end();
}
static struct { uint8_t row, event; } song_punch;
static int song_punch_on(void);                       /* ui_popup.c: the visual section FX editor */
static uint16_t punch_color(uint32_t fx);             /* ui_layer.c: same family colours as live FX */
static uint32_t song_punch_steps_at(uint32_t row)
{
    uint32_t bank = chain_patterns[row][0];
    return bank < NPAT ? (uint32_t)pattern_timing(0, bank, 0) : 16u;
}
static void song_punch_edit(uint32_t field, int32_t delta)
{
    uint32_t row = song_punch.row, j = song_punch.event;
    if (row >= chain_config.count || j >= CHAIN_FX_EVENTS) return;
    chain_fx_t e = chain_config.fx[row][j];
    uint32_t steps = song_punch_steps_at(row);
    if (!field) {
        e.fx = (uint8_t)clamp((int32_t)e.fx + delta, 0, PUNCH_NFX);
        if (!e.fx) memset(&e, 0, sizeof e);
        else if (!e.length) e.length = 1;
    } else {
        if (!e.fx) { ui_message("CHOOSE AN EFFECT"); return; }
        /* Moving a punch retains its duration at the right edge. */
        if (field == 1u) e.start = (uint8_t)clamp((int32_t)e.start + delta, 0,
            steps - (e.length < steps ? e.length : steps));
        else e.length = (uint8_t)clamp((int32_t)e.length + delta, 1, steps);
    }
    if (e.fx) {
        if (e.start >= steps) e.start = (uint8_t)(steps - 1u);
        if (e.length > steps - e.start) e.length = (uint8_t)(steps - e.start);
    }
    if (!memcmp(&e, &chain_config.fx[row][j], sizeof e) || !song_edit_begin()) return;
    chain_config.fx[row][j] = e; sect_sync(row); song_edit_end();
}
static void song_punch_clear(void)
{
    if (song_punch.row >= chain_config.count || !chain_fx_count(song_punch.row) || !song_edit_begin()) return;
    memset(chain_config.fx[song_punch.row], 0, sizeof chain_config.fx[0]); sect_sync(song_punch.row); song_edit_end();
}
static void song_edit_history(int redo)
{
    uint32_t f = motion_guard();
    song_edit_sync();
    if ((!redo && !se.top) || (redo && se.top == se.count)) {
        motion_unguard(f); ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return;
    }
    song_edit_snapshot_t *h = resource[RES_SONG_UNDO].ptr;
    song_edit_snapshot_t *s = &h[redo ? se.top++ : --se.top];
    note_swap(&chain_config, &s->config, sizeof chain_config);
    note_swap(chain_patterns, s->patterns, sizeof chain_patterns);
    uint8_t row = ui.song_row; ui.song_row = s->cursor; s->cursor = row;
    if (sp.open && sp.row >= chain_config.count) sp.open = 0;
    song_edit_cancel(); song_edit_end(); motion_unguard(f);
    ui_message(redo ? "REDO SONG" : "UNDO SONG");
}
static uint32_t song_range_first(void) { return se.range && se.anchor < se.end ? se.anchor : se.range ? se.end : ui.song_row; }
static uint32_t song_range_last(void) { return se.range && se.anchor > se.end ? se.anchor : se.range ? se.end : ui.song_row; }
static int song_range_has(uint32_t r) { return se.range && r >= song_range_first() && r <= song_range_last(); }

/* --------------------------------------------------------------- columns --- */
/* n empty columns at at: the ones there move right */
static void song_rows_open(uint32_t at, uint32_t n)
{
    uint32_t tail = chain_config.count - at;
    song_edit_shift(chain_config.row + at + n, chain_config.row + at, tail * sizeof(chain_row_t));
    song_edit_shift(chain_config.fx + at + n, chain_config.fx + at, tail * sizeof chain_config.fx[0]);
    song_edit_shift(chain_config.sound + at + n, chain_config.sound + at, tail * NTRK);
    song_edit_shift(chain_patterns + at + n, chain_patterns + at, tail * NTRK);
    song_edit_shift(chain_config.sect + at + n, chain_config.sect + at, tail);
    song_edit_shift(chain_config.len + at + n, chain_config.len + at, tail * sizeof chain_config.len[0]);
    chain_config.count += (uint8_t)n;
}
static void song_rows_remove(uint32_t first, uint32_t n)
{
    uint32_t tail = chain_config.count - first - n;
    song_edit_shift(chain_config.row + first, chain_config.row + first + n, tail * sizeof(chain_row_t));
    song_edit_shift(chain_config.fx + first, chain_config.fx + first + n, tail * sizeof chain_config.fx[0]);
    song_edit_shift(chain_config.sound + first, chain_config.sound + first + n, tail * NTRK);
    song_edit_shift(chain_patterns + first, chain_patterns + first + n, tail * NTRK);
    song_edit_shift(chain_config.sect + first, chain_config.sect + first + n, tail);
    song_edit_shift(chain_config.len + first, chain_config.len + first + n, tail * sizeof chain_config.len[0]);
    chain_config.count -= (uint8_t)n;
    for (uint32_t r = chain_config.count; r < CHAIN_ROWS; r++) {
        chain_config.row[r] = (chain_row_t){0, 1}; memset(chain_patterns[r], 0, NTRK);
        memset(chain_config.fx[r], 0, sizeof chain_config.fx[r]);
        memset(chain_config.sound[r], 0, NTRK);
        chain_config.sect[r] = 0; chain_config.len[r] = 0;
    }
    if (ui.song_row > chain_config.count) ui.song_row = chain_config.count;
}
/* column dst becomes a column of src's section (a snapshot's, s, when given) */
static void song_column_put(uint32_t dst, uint32_t src, const song_edit_snapshot_t *s)
{
    const chain_config_t *c = s ? &s->config : &chain_config;
    chain_config.row[dst] = c->row[src];
    memcpy(chain_config.fx[dst], c->fx[src], sizeof chain_config.fx[dst]);
    memcpy(chain_config.sound[dst], c->sound[src], NTRK);
    memcpy(chain_patterns[dst], s ? s->patterns[src] : chain_patterns[src], NTRK);
    chain_config.sect[dst] = c->sect[src]; chain_config.len[dst] = c->len[src];
}
static void song_sections_delete(void)
{
    uint32_t first = song_range_first(), last = song_range_last();
    if (last >= chain_config.count || !song_edit_begin()) return;
    song_rows_remove(first, last - first + 1u); song_edit_cancel(); song_edit_end();
    ui_message("SECTIONS DELETED");
}
static uint32_t song_patterns_now(uint8_t *pat)        /* what every track plays (tracks not in use: P1, as added) */
{
    for (uint32_t k = 0; k < NTRK; k++) pat[k] = k < trk_n() ? trk[k].pattern : 0u;
    uint32_t r = 0;
    while (r < chain_config.count) {                    /* (a section of those patterns: only the tracks in use count) */
        uint32_t k = 0;
        while (k < trk_n() && chain_patterns[r][k] == pat[k]) k++;
        if (k == trk_n()) break;
        r++;
    }
    return r < chain_config.count ? r : CHAIN_ROWS;
}
/* SONG OCT+ on the add column: a section of what every track plays now; those patterns' section again if there is one */
static void song_add_playing(void)
{
    uint8_t pat[NTRK];
    uint32_t at = ui.song_row < chain_config.count ? ui.song_row + 1u : chain_config.count, id = sect_free();
    uint32_t m = song_patterns_now(pat);
    char b[16];
    if (chain_config.count >= CHAIN_ROWS) { ui_message("SONG FULL"); return; }
    if (m == CHAIN_ROWS && id == SONG_SECTIONS) { ui_message("26 SECTIONS IN USE"); return; }
    if (!song_edit_begin()) return;
    song_rows_open(at, 1);
    if (m < CHAIN_ROWS) song_column_put(at, m >= at ? m + 1u : m, 0);
    else {
        memcpy(chain_patterns[at], pat, NTRK);
        memset(chain_config.fx[at], 0, sizeof chain_config.fx[at]);
        memset(chain_config.sound[at], 0, NTRK);
        chain_config.row[at] = (chain_row_t){pat[0], 1};
        chain_config.sect[at] = (uint8_t)id; chain_config.len[at] = 0;
    }
    ui.song_row = (uint8_t)(at + 1u);
    song_edit_cancel(); song_edit_end();
    str_cpy(b, m < CHAIN_ROWS ? "PLACED A" : "ADDED A", sizeof b);
    b[str_len(b) - 1u] = (char)('A' + chain_config.sect[at]);
    ui_message(b);
}
/* KNOB 2 on a column: the next (previous) section in letter order plays there */
static void song_column_section(int32_t steps)
{
    uint32_t row = ui.song_row, id;
    if (row >= chain_config.count) return;
    id = chain_config.sect[row];
    for (uint32_t i = 1; i < SONG_SECTIONS; i++) {
        uint32_t c = steps > 0 ? (chain_config.sect[row] + i) % SONG_SECTIONS : (chain_config.sect[row] + SONG_SECTIONS - i) % SONG_SECTIONS;
        if (sect_first(c) < CHAIN_ROWS) { id = c; break; }
    }
    if (id == chain_config.sect[row]) { ui_message("ONE SECTION SO FAR"); return; }
    if (!song_edit_begin()) return;
    song_column_put(row, sect_first(id), 0);
    song_edit_end();
}
/* Duplicate (a column of the same section after it), Copy as new section (the next free letter, the same contents) */
static void song_insert(uint32_t fresh)
{
    uint32_t first = song_range_first(), last = fresh ? first : song_range_last(), n = last - first + 1u, id = sect_free();
    if (first >= chain_config.count) { ui_message("ADD A SECTION"); return; }
    if (chain_config.count + n > CHAIN_ROWS) { ui_message("SONG FULL"); return; }
    if (fresh && id == SONG_SECTIONS) { ui_message("26 SECTIONS IN USE"); return; }
    if (!song_edit_begin()) return;
    song_rows_open(last + 1u, n);
    for (uint32_t j = 0; j < n; j++) song_column_put(last + 1u + j, first + j, 0);
    if (fresh) chain_config.sect[last + 1u] = (uint8_t)id;
    ui.song_row = (uint8_t)(last + 1u);
    if (fresh && sp.open) sp.row = ui.song_row;        /* (the section page: the copy, to change it) */
    song_edit_cancel(); song_edit_end();
    if (fresh) { char b[16] = "NEW SECTION A"; b[12] = (char)('A' + id); ui_message(b); }
    else ui_message("SECTION DUPLICATED");
}
static void song_arm(uint32_t mode)
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    if (ui.song_row >= chain_config.count) { ui_message("ADD A SECTION"); return; }
    se.first = (uint8_t)song_range_first();
    se.last = (uint8_t)song_range_last();
    se.track = song.sel; se.pending = (uint8_t)mode; se.range = 0; ui.force = 1;
}
static void song_sections_copy(void) { song_arm(SONG_SECTIONS_COPY); }
static void song_sections_move(void) { song_arm(SONG_SECTIONS_MOVE); }
static void song_apply_destination(void)
{
    uint32_t mode = se.pending, dest = ui.song_row, n = se.last - se.first + 1u;
    if (!mode) return;
    if (mode == SONG_SECTIONS_MOVE && dest >= se.first && dest <= se.last + 1u) { song_edit_cancel(); return; }
    if (mode == SONG_SECTIONS_COPY && chain_config.count + n > CHAIN_ROWS) { ui_message("SONG FULL"); return; }
    if (!song_edit_begin()) return;
    const song_edit_snapshot_t *s = &((song_edit_snapshot_t *)resource[RES_SONG_UNDO].ptr)[se.top - 1u];
    if (mode == SONG_SECTIONS_MOVE) {
        song_rows_remove(se.first, n);
        if (dest > se.last) dest -= n;
    }
    song_rows_open(dest, n);
    for (uint32_t j = 0; j < n; j++) song_column_put(dest + j, se.first + j, s);
    ui.song_row = (uint8_t)dest;
    song_edit_cancel(); song_edit_end(); ui_message("SONG UPDATED");
}

/* ---------------------------------------------------------- section page --- */
static void sect_open(uint32_t row)
{
    if (row >= chain_config.count) return;
    sp.open = 1; sp.row = (uint8_t)row; ui.song_row = (uint8_t)row;
    se.pending = se.range = 0; ui.force = 1;
}
static void sect_close(void)
{
    if (sect_looping()) transport_req = 2;
    sp.open = 0; ui.force = 1;
}
/* KNOB k: track k's pattern in the section, REST below P1 (every column of it) */
static void sect_pattern(uint32_t k, int32_t steps)
{
    uint32_t row = sp.row;
    if (k >= trk_n()) return;
    int32_t old = chain_patterns[row][k] >= NPAT ? -1 : chain_patterns[row][k];
    int32_t value = clamp(old + steps, -1, (int32_t)trk_npat(k) - 1);
    if (value == old || !song_edit_begin()) return;
    chain_patterns[row][k] = (uint8_t)(value < 0 ? CHAIN_REST : value);
    if (value < 0) chain_config.sound[row][k] = 0;
    chain_config.row[row].slot = chain_patterns[row][0];
    sect_sync(row);
    if (sect_looping()) chain.len_dirty = 1;
    song_edit_end();
}
/* SELECT: the length a beat a detent (from AUTO: its length now); turned below a beat: AUTO */
static void sect_length(int32_t steps)
{
    uint32_t row = sp.row, len = chain_config.len[row];
    int32_t beats = (int32_t)(len ? (len + 3u) / 4u : (sect_len_u(row) + 23u) / 24u);
    if (!len && steps < 0 && beats <= 1) return;
    beats += steps;
    uint32_t to = beats < 1 ? 0u : (uint32_t)clamp(beats, 1, SECT_LEN_MAX / 4u) * 4u;
    if (to == len || !song_edit_begin()) return;
    chain_config.len[row] = (uint16_t)to;
    sect_sync(row); song_edit_end();
}
static void sect_auto(void)
{
    uint32_t row = sp.row;
    if (!chain_config.len[row] || !song_edit_begin()) return;
    chain_config.len[row] = 0; sect_sync(row); song_edit_end();
    ui_message("LENGTH: AUTO");
}
/* PLAY on the section page: this section again and again (again: stop) */
static void sect_play(void)
{
    if (song.playing || seq_counting() || chain_busy()) { transport_req = 2; return; }
    uint32_t rc = chain_prepare_solo(sp.row, 1);
    if (rc == 67u) ui_message("NO SOUND MEMORY");
    else if (rc) ui_message(rc == 1u ? "ADD A SECTION" : "STOP FIRST");
    ui.force = 1;
}

/* ----------------------------------------------------------- live launch --- */
/* SONG REC: the column's section for every track at once; stopped: from the top, playing: the next bar, or the end of
 * the section launched before */
static void song_launch(void)
{
    uint32_t row = ui.song_row;
    if (chain_busy()) { ui_message("STOP SONG TO LAUNCH"); return; }
    if (row >= chain_config.count) { ui_message("ADD A SECTION"); return; }
    uint32_t f = motion_guard();
    if (!song.playing && !seq_counting()) {
        for (uint32_t k = 0; k < NTRK; k++) pattern_prepare(k, chain_patterns[row][k]);
        fm1_irq_off(); live_apply(row, 0); fm1_irq_on();
        transport_req = 1;
    } else live.next = (uint8_t)row;
    motion_unguard(f);
    ui.force = 1;
}
/* the launched section, still as it is: its patterns, its rests */
static int live_changed(void)
{
    uint32_t row = live.cur;
    if (row >= chain_config.count) return 0;
    for (uint32_t k = 0; k < trk_n(); k++) {
        uint32_t bank = chain_patterns[row][k];
        if (bank < NPAT ? trk[k].pattern != bank || trk[k].pattern_next < NPAT : !((live.rest >> k) & 1u)) return 1;
    }
    return 0;
}

static const char *song_edit_status(void)
{
    static char b[16];
    if (song_picker.active) return "SET PRESET";
    if (chain.running && chain.solo) { str_cpy(b, "LOOPING A", sizeof b); b[8] = (char)('A' + chain.config.sect[chain.row]); return b; }
    if (chain.running && chain.config.rsv[CHAIN_LOOP]) return "SONG LOOP";
    if (!chain.running && live.next < chain_config.count) { str_cpy(b, "A NEXT", sizeof b); b[0] = (char)('A' + chain_config.sect[live.next]); return b; }
    if (!chain.running && live.cur < chain_config.count && live_changed()) { str_cpy(b, "A CHANGED", sizeof b); b[0] = (char)('A' + chain_config.sect[live.cur]); return b; }
    if (ui.home || cur_page()->graph != GR_SONG) return "";
    if (se.pending) return se.pending == SONG_SECTIONS_MOVE ? "MOVE TO..." : "COPY TO...";
    if (se.range) return "SELECTED";
    if (chain_config.rsv[CHAIN_LOOP]) return "LOOP";
    if (sect_on()) {
        const song_sound_t *sound = song_sound_get(chain_config.sound[sp.row][song.sel]);
        if (sound) return sound->name;
    }
    return "";
}
