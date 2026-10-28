/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SONG arranges the current project's banks: columns of sections, a section one bank (or a rest) per track.
 * Main-loop preparation commits the original selections. The ISR switches
 * bounded in-RAM patterns and timing; STOP restores the original selections. */
static chain_config_t chain_config;
#include "song_sound.c"
#include "pattern.c"
static struct {
    uint8_t original[NTRK];
    chain_config_t config;
    int16_t timing[NTRK][4];
    volatile uint8_t armed, running, row, remaining;
    uint8_t slot, rec, start, seek_chase, fx_event;
    uint8_t solo;                       /* the section page's PLAY: the start column again and again */
    uint16_t fx_loop;                   /* the conductor's loops so far: a punch restarts with each */
    volatile uint8_t len_dirty;         /* an edit while it loops: its length again */
    uint32_t carry, rest_pos, seek_phase[NTRK];
    uint32_t len, len_beat;             /* the section playing: its samples, at that beat */
    uint64_t seek_samples[NTRK];
} chain;
/* Live launch (SONG REC while the song is not playing): every track switches to a column's section together, the first
 * on the next bar, the next at the end of the one launched. Patterns only: a section's sounds and punches play with
 * the song. A track changed by hand afterwards leaves the section (the header: "A CHANGED") */
static struct { volatile uint8_t next, cur, rest; uint32_t pos, carry; } live = {CHAIN_ROWS, CHAIN_ROWS, 0, 0, 0};

static void seq_release(track_t *t);
static void seq_stop(void);
static uint32_t div_samples(uint32_t div);
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx);
static void pattern_switch(track_t *t, uint32_t bank);
static volatile uint32_t beat_pos, beat_n;      /* clock units into the beat, beat of the bar (0..3): seq.c */
static volatile uint16_t seq_loops[NTRK];       /* (seq.c: each track's loop ends) */

static void chain_defaults(chain_config_t *c)
{
    uint32_t i;
    memset(c, 0, sizeof *c);
    for (i = 0; i < CHAIN_ROWS; i++)
        c->row[i].repeat = 1;
}
static int chain_fx_valid(const chain_fx_t *e)
{
    return e->fx ? e->fx <= PUNCH_NFX && e->start < NSTEP && e->length &&
        e->length <= NSTEP - e->start : !e->start && !e->length;
}
static uint32_t chain_fx_count(uint32_t row)
{
    uint32_t n = 0;
    for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) n += chain_config.fx[row][j].fx != 0;
    return n;
}
static int chain_valid(const chain_config_t *c)
{
    uint32_t i;
    if (c->count > CHAIN_ROWS || c->rsv[CHAIN_LOOP] > 1u)
        return 0;
    for (i = 0; i < c->count; i++)
        if (c->row[i].slot > CHAIN_HOLD || !c->row[i].repeat || c->row[i].repeat > 16u ||
            (c->row[i].slot == CHAIN_HOLD && (!i || c->row[i-1u].slot == CHAIN_REST)))
            return 0;
    for (i = 0; i < CHAIN_ROWS; i++)
        for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++)
            if (!chain_fx_valid(&c->fx[i][j]) || (i >= c->count && c->fx[i][j].fx)) return 0;
    for (i = 0; i < CHAIN_ROWS; i++) for (uint32_t k = 0; k < NTRK; k++)
        if (c->sound[i][k] > CHAIN_SOUNDS || (i >= c->count && c->sound[i][k])) return 0;
    for (i = 0; i < CHAIN_ROWS; i++)
        if (c->sect[i] >= SONG_SECTIONS || c->len[i] > SECT_LEN_MAX) return 0;
    return 1;
}
static void chain_legacy_defaults(chain_legacy_t *c)
{
    memset(c, 0, sizeof *c);
    for (uint32_t i = 0; i < CHAIN_LEGACY_ROWS; i++) c->row[i].repeat = 1;
}
static int chain_legacy_valid(const chain_legacy_t *c)
{
    if (c->count > CHAIN_LEGACY_ROWS) return 0;
    for (uint32_t i = 0; i < c->count; i++)
        if (c->row[i].slot >= NPAT || !c->row[i].repeat || c->row[i].repeat > 16u) return 0;
    return 1;
}
static void chain_from_legacy(const chain_legacy_t *c)
{
    chain_defaults(&chain_config);
    chain_config.count = c->count;
    memcpy(chain_config.row, c->row, sizeof c->row);
}
static int chain_rest(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk);
    return chain.running ? chain_patterns[chain.row][k] == CHAIN_REST : (live.rest >> k) & 1u;
}
static uint32_t chain_origin(uint32_t row, uint32_t track)
{
    while (row && chain_patterns[row][track] == CHAIN_HOLD) row--;
    return row;
}
/* Earlier song copies may name only the first section of a spanning block.
 * Empty continued cells inherit that block's sound instead of restoring early. */
static uint32_t chain_sound_id(uint32_t row, uint32_t track)
{
    uint32_t id = chain_config.sound[row][track];
    return id ? id : chain_config.sound[chain_origin(row, track)][track];
}
static uint32_t chain_bank(uint32_t row, uint32_t track)
{
    return chain_patterns[chain_origin(row, track)][track];
}
static uint32_t chain_block_end(uint32_t row, uint32_t track)
{
    while (row + 1u < chain_config.count && chain_patterns[row+1u][track] == CHAIN_HOLD) row++;
    return row + 1u;
}

/* ------------------------------------------------------------ sections --- */
/* a step of each N_DIV division in 1/384 of four bars (a sixteenth is 6): lengths compared exactly, tempo apart */
static const uint16_t SECT_DIV_U[10] = {24, 12, 6, 3, 8, 4, 48, 96, 192, 384};
static uint32_t sect_pattern_u(uint32_t k, uint32_t bank)
{
    uint32_t div = (uint32_t)pattern_timing(k, bank, 1);
    return (uint32_t)pattern_timing(k, bank, 0) * SECT_DIV_U[div < 10u ? div : 2u];
}
/* AUTO: the longest pattern of the tracks in use (all resting: one bar) */
static uint32_t sect_auto_u(const uint8_t *pat)
{
    uint32_t most = 0;
    for (uint32_t k = 0; k < trk_n() && k < NTRK; k++)
        if (pat[k] < NPAT) { uint32_t u = sect_pattern_u(k, pat[k]); if (u > most) most = u; }
    return most ? most : 96u;
}
static uint32_t sect_len_u(uint32_t row)
{
    return chain_config.len[row] ? chain_config.len[row] * 6u : sect_auto_u(chain_patterns[row]);
}
/* track k's pattern in its section: shorter loops, longer is cut (the section page's timeline) */
static uint32_t sect_cut(uint32_t row, uint32_t k)
{
    uint32_t bank = chain_patterns[row][k];
    return bank < NPAT && sect_pattern_u(k, bank) > sect_len_u(row);
}
static int sect_same(uint32_t a, uint32_t b)
{
    return !memcmp(chain_patterns[a], chain_patterns[b], NTRK) && !memcmp(chain_config.sound[a], chain_config.sound[b], NTRK) &&
        !memcmp(chain_config.fx[a], chain_config.fx[b], sizeof chain_config.fx[a]) && chain_config.len[a] == chain_config.len[b];
}
static uint32_t sect_first(uint32_t id)          /* its first column, CHAIN_ROWS: not in the song */
{
    for (uint32_t r = 0; r < chain_config.count; r++) if (chain_config.sect[r] == id) return r;
    return CHAIN_ROWS;
}
static uint32_t sect_uses(uint32_t id)
{
    uint32_t n = 0;
    for (uint32_t r = 0; r < chain_config.count; r++) n += chain_config.sect[r] == id;
    return n;
}
static uint32_t sect_free(void)                  /* the first letter no column uses, SONG_SECTIONS: none */
{
    uint32_t id = 0;
    while (id < SONG_SECTIONS && sect_first(id) < CHAIN_ROWS) id++;
    return id;
}
/* the column whose section plays these patterns, CHAIN_ROWS: none */
static uint32_t sect_match(const uint8_t *pat)
{
    for (uint32_t r = 0; r < chain_config.count; r++) if (!memcmp(chain_patterns[r], pat, NTRK)) return r;
    return CHAIN_ROWS;
}
/* row's section as it is now in every column of it */
static void sect_sync(uint32_t row)
{
    for (uint32_t r = 0; r < chain_config.count; r++) {
        if (r == row || chain_config.sect[r] != chain_config.sect[row]) continue;
        memcpy(chain_patterns[r], chain_patterns[row], NTRK);
        memcpy(chain_config.sound[r], chain_config.sound[row], NTRK);
        memcpy(chain_config.fx[r], chain_config.fx[row], sizeof chain_config.fx[r]);
        chain_config.len[r] = chain_config.len[row];
        chain_config.row[r] = chain_config.row[row];
    }
}
static int sect_consistent(void)
{
    for (uint32_t r = 0; r < chain_config.count; r++) {
        if (chain_config.sect[r] >= SONG_SECTIONS || chain_config.row[r].repeat != 1u) return 0;
        uint32_t f = sect_first(chain_config.sect[r]);
        if (!sect_same(f, r)) return 0;
        for (uint32_t k = 0; k < NTRK; k++) if (chain_patterns[r][k] > CHAIN_REST) return 0;
    }
    return 1;
}
/* After a load or the editor's rows: the song as sections. A block held over columns gives each its pattern (and so
 * restarts there); a column plays its old length, track 1's pattern (resting or held: a bar) times its repeats,
 * AUTO where that is its longest pattern; identical columns share a letter, in order. Past 26 sections the song ends.
 * keep: the letters as stored, when they still describe the columns. -> SN_HELD, SN_CUT */
enum { SN_HELD = 1, SN_CUT = 2 };
static uint8_t song_note;                       /* a load's conversion (SN_*): the UI says it once */
static uint32_t song_sections_normalize(int keep)
{
    uint32_t n = chain_config.count, out = 0;
    if (keep && sect_consistent()) return 0;
    for (uint32_t r = 0; r < n; r++) {
        uint32_t bank = chain_patterns[r][0], rep = chain_config.row[r].repeat ? chain_config.row[r].repeat : 1u;
        uint32_t u = (bank < NPAT ? sect_pattern_u(0, bank) : 96u) * rep;   /* (before the held cells get patterns) */
        for (uint32_t k = 0; k < NTRK; k++) {
            if (chain_patterns[r][k] == CHAIN_HOLD) {
                chain_patterns[r][k] = r ? chain_patterns[r - 1u][k] : (uint8_t)CHAIN_REST;
                if (!chain_config.sound[r][k] && r) chain_config.sound[r][k] = chain_config.sound[r - 1u][k];
                out |= SN_HELD;
            } else if (chain_patterns[r][k] > CHAIN_REST) chain_patterns[r][k] = CHAIN_REST;
        }
        if (!keep || rep > 1u) chain_config.len[r] = (uint16_t)(u == sect_auto_u(chain_patterns[r]) ? 0u : u < 6u ? 1u : (u + 3u) / 6u);
        if (chain_config.len[r] > SECT_LEN_MAX) chain_config.len[r] = SECT_LEN_MAX;
        chain_config.row[r] = (chain_row_t){chain_patterns[r][0], 1};
    }
    uint32_t ids = 0;
    for (uint32_t r = 0; r < n; r++) {
        uint32_t q = 0;
        while (q < r && !sect_same(q, r)) q++;
        if (q < r) { chain_config.sect[r] = chain_config.sect[q]; continue; }
        if (ids == SONG_SECTIONS) { chain_config.count = (uint8_t)r; out |= SN_CUT; break; }
        chain_config.sect[r] = (uint8_t)ids++;
    }
    for (uint32_t r = chain_config.count; r < CHAIN_ROWS; r++) {
        chain_config.row[r] = (chain_row_t){0, 1};
        memset(chain_config.fx[r], 0, sizeof chain_config.fx[r]);
        memset(chain_config.sound[r], 0, NTRK);
        memset(chain_patterns[r], 0, NTRK);
        chain_config.sect[r] = 0; chain_config.len[r] = 0;
    }
    return out;
}

/* one pattern's loop in samples at the tempo (an odd LEN: its last swung step too) */
static uint32_t chain_pattern_samples(uint32_t k, uint32_t bank)
{
    uint32_t len = (uint32_t)pattern_timing(k, bank, 0);
    uint32_t period = seq_div_samples((uint32_t)pattern_timing(k, bank, 1));
    int32_t swing = clamp(pattern_timing(k, bank, 2) + song.g[G_SWING], 0, SWING_MAX);
    return len * period + (len & 1u) * (uint32_t)(swing * (int32_t)period / 250);
}
/* a column's section in samples: its set length, else its longest pattern (all resting: a bar) */
static uint32_t chain_row_samples_of(const chain_config_t *c, uint32_t row)
{
    if (c->len[row]) return c->len[row] * seq_div_samples(2u);
    uint32_t most = 0;
    for (uint32_t k = 0; k < trk_n() && k < NTRK; k++) {
        uint32_t bank = chain_patterns[row][k];
        if (bank < NPAT) { uint32_t s = chain_pattern_samples(k, bank); if (s > most) most = s; }
    }
    return most ? most : 16u * seq_div_samples(2u);
}
static uint64_t chain_row_samples(uint32_t row) { return chain_row_samples_of(&chain_config, row); }
/* Long arrangements exceed 32-bit samples. The target has no 64-bit divide
 * runtime; the common short case still uses its native 32-bit operation. */
static uint32_t chain_div_samples(uint64_t value, uint32_t divisor, uint32_t *remainder)
{
    if (value <= 0xffffffffu) {
        if (remainder) *remainder = (uint32_t)value % divisor;
        return (uint32_t)value / divisor;
    }
    uint32_t q = 0, r = 0;
    for (int bit = 63; bit >= 0; bit--) {
        r = (r << 1) | (uint32_t)(value >> bit & 1u);
        uint32_t take = r >= divisor;
        if (take) r -= divisor;
        q = (q << 1) | take;
    }
    if (remainder) *remainder = r;
    return q;
}
static const step_t *seq_steps(const track_t *t)
{
    return t->step;
}
static void motion_restore(track_t *t);
static void song_sound_motion_chase(track_t *t);
/* every track from its step 0: the column's pattern, a rest silent */
static void chain_apply(int starting)
{
    uint32_t i;
    (void)starting;
    chain.slot = chain.config.row[chain.row].slot;
    chain.rest_pos = chain.carry;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        int change = song_sound_changed(chain.row, i);
        seq_release(t);
        motion_restore(t);
        if (change) song_sound_section(chain.row, i);
        recording_stop(t);
        uint32_t bank = chain_patterns[chain.row][i];
        if (bank < NPAT) pattern_apply(t, bank);
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu;
        t->rh_n = t->rskip_n = 0;
    }
    chain.len = chain_row_samples_of(&chain.config, chain.row);
    chain.len_beat = seq_beat_samples(); chain.len_dirty = 0;
    chain.fx_loop = chain_patterns[chain.row][0] >= NPAT ? 0u : seq_loops[0];
}
static void chain_start(void)
{
    uint32_t i;
    if (!chain.armed)
        return;
    chain.rec = song.rec;
    song.rec = 0;
    for (i = 0; i < NTRK; i++) {
        chain.original[i] = trk[i].pattern;
        memcpy(chain.timing[i], &trk[i].p[P_SLEN], sizeof chain.timing[i]);
    }
    chain.row = chain.start;
    chain.remaining = 1;
    chain.carry = 0;
    chain.running = 1;
    chain.armed = 0;
    punch_song_buffered = 0;
    chain.fx_event = 0;
    for (uint32_t r = 0; r < chain.config.count; r++)
        for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++)
            if (chain.config.fx[r][j].fx && punch_needs_ring(chain.config.fx[r][j].fx - 1)) punch_song_buffered = 1;
    chain_apply(1);
}
static void chain_stop(void)
{
    uint32_t i;
    chain.armed = 0;
    punch_song_req = -1; punch_song_buffered = 0;
    if (!chain.running) { chain.solo = 0; song_sound_stop(); return; }
    song_sound_stop();
    for (i = 0; i < NTRK; i++) {
        pattern_apply(&trk[i], chain.original[i]);
        memcpy(&trk[i].p[P_SLEN], chain.timing[i], sizeof chain.timing[i]);
    }
    song.rec = chain.rec;
    chain.running = 0;
    chain.solo = 0;
}
/* Called after the track clocks advance, including the first step and seeks.
 * Four bounded comparisons; overlapping punches use the latest start, then
 * the later event slot. Releasing a live key resumes the current song punch.
 * The steps are track 1's (resting: a bar's sixteenths), as each loop of its pattern repeats them. */
static void chain_fx_tick(void)
{
    if (!chain.running) return;
    uint32_t rest = chain_patterns[chain.row][0] >= NPAT, sixteenth = seq_div_samples(2u);
    uint32_t step = rest ? (chain.rest_pos / sixteenth) % 16u : trk[0].seq_idx;
    uint16_t loop = (uint16_t)(rest ? chain.rest_pos / (16u * sixteenth) : seq_loops[0]);
    if (loop != chain.fx_loop) { chain.fx_loop = loop; chain.fx_event = 0; }   /* (a new loop: its punches from the top) */
    uint32_t latest = 0, event = 0;
    punch_song_req = -1;
    for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) {
        const chain_fx_t *e = &chain.config.fx[chain.row][j];
        if (e->fx && step >= e->start && step - e->start < e->length && e->start >= latest) {
            punch_song_req = (int8_t)(e->fx - 1u); latest = e->start; event = j + 1u;
        }
    }
    if (chain.fx_event != event) { chain.fx_event = (uint8_t)event; punch_song_gate++; }
}
/* the section clock: at its length every track starts the next column (the section page's PLAY: this one again) */
static void chain_tick(uint32_t n)
{
    const track_t *t = &trk[0];
    if (!chain.running || t->seq_pos >= 0x7FFFFFFFu) return;
    if (chain.len_dirty || chain.len_beat != seq_beat_samples()) {
        chain.len = chain_row_samples_of(&chain.config, chain.row);
        chain.len_beat = seq_beat_samples(); chain.len_dirty = 0;
    }
    uint32_t position = chain.rest_pos;
    chain.rest_pos += n;
    if (position + n < chain.len) return;
    chain.fx_event = 0;
    chain.carry = position + n - chain.len;
    if (chain.solo) { chain_apply(0); return; }
    if (chain.row + 1u >= chain.config.count) {
        if (!chain.config.rsv[CHAIN_LOOP]) { seq_stop(); return; }
        chain.row = 0;
    } else chain.row++;
    chain_apply(0);
}

/* ---------------------------------------------------------- live launch --- */
static void live_reset(void) { live.next = live.cur = CHAIN_ROWS; live.rest = 0; live.pos = live.carry = 0; }
/* every track to column row's pattern from its step 0, carry samples in (the ISR, or stopped) */
static void live_apply(uint32_t row, uint32_t carry)
{
    uint8_t rest = 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t bank = chain_patterns[row][k];
        seq_release(t); motion_restore(t); recording_stop(t);
        if (bank < NPAT) pattern_switch(t, bank);
        else if (k < trk_n()) rest |= (uint8_t)(1u << k);
        t->pattern_next = 0xff;
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu;
        t->rh_n = t->rskip_n = 0;
    }
    live.rest = rest; live.cur = (uint8_t)row; live.next = CHAIN_ROWS;
    live.pos = carry; live.carry = carry;
}
static void live_tick(uint32_t n)
{
    if (chain.running || chain.armed || !song.playing || (live.cur >= CHAIN_ROWS && live.next >= CHAIN_ROWS)) return;
    uint32_t len = live.cur < CHAIN_ROWS ? chain_row_samples_of(&chain_config, live.cur) : 0u, remain;
    if (live.cur < CHAIN_ROWS) {
        if (live.pos >= len) live.pos %= len;
        remain = len - live.pos;
    } else {                                    /* the first: on the next bar */
        uint32_t beat = seq_beat_samples();
        remain = (3u - (beat_n & 3u)) * beat + (beat > beat_pos ? beat - beat_pos : 0u);
    }
    if (live.next >= CHAIN_ROWS || live.next >= chain_config.count || n < remain) {
        if (live.next < CHAIN_ROWS && live.next >= chain_config.count) live.next = CHAIN_ROWS;
        if (live.cur < CHAIN_ROWS) live.pos += n;
        return;
    }
    live_apply(live.next, n - remain);
}

/* Foreground preparation follows a queued manual choice or SONG's next row.
 * STOP can always restore the original selections through the bounded fallback. */
static void pattern_poll(void)
{
    song_base_reserved = 0;
    if (!song_sound_count) resource_release(RES_SONG_SOUNDS);
    if (!song_sounds.mask && !chain.armed && !pattern_sounds_any()) resource_release(RES_SONG_BASE);
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t bank = trk[k].pattern_next;
        if (chain.armed) bank = chain_bank(chain.start, k);
        else if (chain.running) {
            uint32_t row = chain.solo ? chain.row : chain.row + 1u;
            bank = row < chain.config.count ? chain_bank(row, k) :
                chain.config.rsv[CHAIN_LOOP] ? chain_bank(0, k) : chain.original[k];
        } else if (live.next < chain_config.count) bank = chain_patterns[live.next][k];
        pattern_prepare(k, bank);
    }
}
