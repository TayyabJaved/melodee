/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SEQ > CHORDS (ui_chords.c): a step of one note may carry a chord to play on it, with its voicing, strum, direction and
 * nudge. They live in the note bytes such a step leaves unused (note[1..3]: 21 bits, kept exactly by the project and
 * bank stores, the inactive-pattern pool, undo and the editor): 15 bits of settings and a 6-bit check, so what a
 * deleted chord left there is not taken for one (ui.c step_clear and the entries clear them too). Included by seq.c;
 * the playback half runs in the audio ISR.
 *   CHORD: OFF or DIA3 .. POW (chord.c's shapes; DIA3 / DIA7 in the track's scale); VOICE: chord.c's voicings;
 *   STRUM: the sweep, as SCL > CHORD's; DIR: DOWN UP ALT; NUDGE: -12..12 24ths of a step, early or late.
 * An entry plays as a chord key does (MONO / LEGATO / UNISON: its root; a kit: never a chord), its notes released
 * together. A late entry waits in the step; an early one plays in the step before (not across a queued pattern, a
 * song, a generator). Each wait is a boundary of the clock's advance (seq.c chord_strum_next): exact to the sample. */
enum { CS_CHORD, CS_VOICE, CS_STRUM, CS_DIR, CS_NUDGE, CS_N };   /* an entry's settings, in its 15 bits */
static const uint8_t CS_RANGE[CS_N] = {10, 5, 8, 3, 25};
#define CS_REST 12u                             /* NUDGE: 12 + 24ths of a step, each way */
#define CS_VALUES (10u * 5u * 8u * 3u * 25u)
_Static_assert(CS_VALUES <= 0x8000u && NELEM(N_STRUM) == 8u && NELEM(N_VOIC) == 5u && NELEM(N_STROKE) == 3u &&
               CH_POW == 9 && NTRK <= 8u, "chord entry fields");
typedef struct { uint8_t v[CS_N]; } cseq_t;      /* CHORD: 0 none, k chord.c's CH_* k (DIA3 .. POW) */

static uint32_t cseq_check(uint32_t v) { return ((v * 2654435761u) >> 26) ^ 0x2Du; }   /* (never 0 for the rest:
                                                                                       * zeroed bytes are no entry) */
static void cseq_rest(cseq_t *c)                /* nothing to it: a plain note */
{
    memset(c, 0, sizeof *c);
    c->v[CS_NUDGE] = CS_REST;
}
static int32_t cseq_nudge(const cseq_t *c) { return (int32_t)c->v[CS_NUDGE] - (int32_t)CS_REST; }
/* step s an entry -> 1, its settings in *c */
static int cseq_get(const step_t *s, cseq_t *c)
{
    uint32_t w, v, i;
    if (s->n != 1u || s->time != ST_NOTE || s->hit || (s->flags & SF_RECORDED))
        return 0;
    w = (s->note[1] & 127u) | (uint32_t)(s->note[2] & 127u) << 7 | (uint32_t)(s->note[3] & 127u) << 14;
    v = w & 0x7FFFu;
    if (v >= CS_VALUES || w >> 15 != cseq_check(v))
        return 0;
    for (i = 0; i < CS_N; i++) {
        c->v[i] = (uint8_t)(v % CS_RANGE[i]);
        v /= CS_RANGE[i];
    }
    return 1;
}
/* step s (one note) takes settings c; all at rest: a plain note again */
static void cseq_put(step_t *s, const cseq_t *c)
{
    uint32_t v = 0, w, i = CS_N, rest = 1;
    while (i--) {
        v = v * CS_RANGE[i] + c->v[i];
        rest &= c->v[i] == (i == CS_NUDGE ? CS_REST : 0u);
    }
    w = rest ? 0u : v | cseq_check(v) << 15;
    s->note[1] = (uint8_t)(w & 127u);
    s->note[2] = (uint8_t)((w >> 7) & 127u);
    s->note[3] = (uint8_t)(w >> 14);
}
/* the notes settings c play on root (ascending; 1..CHORD_MAX) */
static uint32_t cseq_chord(const track_t *t, uint32_t root, const cseq_t *c, uint8_t *out)
{
    int32_t r;
    uint16_t mask;
    if (!c->v[CS_CHORD]) {
        out[0] = (uint8_t)root;
        return 1u;
    }
    return chord_make_as(t, root, c->v[CS_CHORD], c->v[CS_VOICE], out, &r, &mask);
}
/* the NUDGE of a step in the clock's units */
static uint32_t cseq_nudge_n(const track_t *t, int32_t nudge)
{
    return seq_div_samples((uint32_t)t->p[P_SDIV]) * (uint32_t)(nudge < 0 ? -nudge : nudge) / 24u;
}

static struct {
    chord_strum_t strum[NTRK];
    uint8_t note[NTRK][CHORD_MAX], started[NTRK], vel[NTRK];   /* a sweep's notes in its order, those begun */
    uint32_t late[NTRK];                        /* a late entry: what it waits yet */
    uint8_t late_idx[NTRK], early[NTRK];        /* .. its step; the step played early (+1, 0 none) */
    uint8_t strumming, waiting, firing;         /* bit per track: a sweep under way, a late entry; firing: cseq_fire */
    volatile uint8_t live;                      /* the track (+1) whose keys play the next entry's chord (ui_chords.c) */
    cseq_t pen;                                 /* .. that chord */
} cseq __attribute__((section(".pool")));

/* seq_step, a NOTE step's notes. An entry: its chord into nn / vv, the strings the sweep plays later marked in *sk
 * (they do not start now) -> their number; 0: no entry (its notes as stored). A NOTE ends the sweep before */
static uint32_t cseq_notes(track_t *t, const step_t *s, uint8_t *nn, uint8_t *vv, uint32_t vel, uint32_t *sk)
{
    uint32_t k = trk_index(t), n, now, i;
    cseq_t c;
    cseq.strumming &= (uint8_t)~(1u << k);
    if (!cseq_get(s, &c))
        return 0;
    n = cseq_chord(t, s->note[0], &c, cseq.note[k]);
    now = chord_strum_begin_as(t, cseq.note[k], n, &cseq.strum[k], vel, c.v[CS_STRUM], c.v[CS_DIR], 0);
    for (i = 0; i < n; i++) {
        nn[i] = cseq.note[k][i];
        vv[i] = (uint8_t)vel;
        if (i >= now)
            *sk |= 1u << i;
    }
    if (now < n) {
        cseq.started[k] = (uint8_t)now;
        cseq.vel[k] = (uint8_t)vel;
        cseq.strum[k].pos = cseq.firing || t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;   /* (the step began so long
                                                                                          * ago) */
        cseq.strumming |= (uint8_t)(1u << k);
    }
    return n;
}

static void seq_step(track_t *t, const step_t *s, uint32_t period, uint32_t skip);
static int gn_active(const track_t *t);
/* play step idx now, as the sequencer would on entering it; late by `late` in the clock's advance being counted */
static void cseq_fire(track_t *t, uint32_t idx, uint32_t period, uint32_t late)
{
    const step_t *s = &t->step[idx];
    uint16_t at = t->seq_idx;
    t->seq_idx = (uint16_t)idx;                 /* (seq_step looks at the step after it: a TIE holds on) */
    cseq.firing = 1;
    seq_step(t, s, step_gate(s) ? step_samples(t, period, idx) : period, 0);
    cseq.firing = 0;
    t->seq_idx = at;
    if (t->seq_n)
        t->seq_off += late;                     /* (seq_grid_tick counts this advance off its gate next) */
}
/* step j may play in the step before it */
static int cseq_early_ok(const track_t *t, uint32_t j)
{
    return !gn_active(t) && !chain.running && !((seq_hush >> trk_index(t)) & 1u) && !(j == 0u && t->pattern_next < NPAT);
}
/* the next step an early entry -> how early it plays, 0 none */
static uint32_t cseq_early(const track_t *t, uint32_t *j)
{
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    cseq_t c;
    *j = (t->seq_idx + 1u) % len;
    if (cseq.early[trk_index(t)] == *j + 1u || !cseq_early_ok(t, *j) || !cseq_get(&t->step[*j], &c) || cseq_nudge(&c) >= 0)
        return 0;
    return cseq_nudge_n(t, cseq_nudge(&c));
}

/* seq_grid_tick, first: n more of the clock. The sweep's strings, a late entry, an early one due by its end */
static void cseq_tick(track_t *t, uint32_t n)
{
    uint32_t k = trk_index(t), b = 1u << k, period, ahead, cur, j;
    if (!song.playing || t->seq_pos >= 0x7FFFFFFFu) {   /* stopped, starting: nothing waits */
        cseq.strumming &= (uint8_t)~b;
        cseq.waiting &= (uint8_t)~b;
        cseq.early[k] = 0;
        return;
    }
    if (cseq.strumming & b) {
        chord_strum_t *s = &cseq.strum[k];
        if (!t->seq_n) {                        /* released: the rest stays silent */
            cseq.strumming &= (uint8_t)~b;
        } else {
            chord_strum_due(s, cseq.started[k], n);
            while (cseq.started[k] < s->total && !chord_strum_due(s, cseq.started[k], 0)) {
                uint8_t x = cseq.note[k][cseq.started[k]++];
                trk_note_on(t, x, cseq.vel[k]);
                if (t->seq_n < sizeof t->seq_notes)
                    t->seq_notes[t->seq_n++] = x;
            }
            if (cseq.started[k] >= s->total)
                cseq.strumming &= (uint8_t)~b;
        }
    }
    period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    if (cseq.waiting & b) {
        if (cseq.late[k] > n) {
            cseq.late[k] -= n;
        } else {
            cseq.waiting &= (uint8_t)~b;
            cseq_fire(t, cseq.late_idx[k], period, n);
        }
    }
    ahead = cseq_early(t, &j);
    cur = step_samples(t, period, t->seq_idx);
    if (ahead && t->seq_pos + n < cur && cur - (t->seq_pos + n) <= ahead) {
        cseq.early[k] = (uint8_t)(j + 1u);
        cseq_fire(t, j, period, n);
    }
}

/* the sequencer entering its step (s: what plays there) -> 1: not now. Played early already, or late: it waits */
static int cseq_hold(track_t *t, const step_t *s, uint32_t period)
{
    uint32_t k = trk_index(t), idx = t->seq_idx, wait;
    cseq_t c;
    if (cseq.waiting & (1u << k)) {             /* (a swung step shorter than the nudge before) */
        cseq.waiting &= (uint8_t)~(1u << k);
        cseq_fire(t, cseq.late_idx[k], period, 0);
    }
    if (cseq.early[k]) {
        uint32_t e = cseq.early[k] - 1u;
        cseq.early[k] = 0;
        if (e == idx)
            return 1;
    }
    if (s != &t->step[idx] || !cseq_get(s, &c) || cseq_nudge(&c) <= 0)
        return 0;
    wait = cseq_nudge_n(t, cseq_nudge(&c));
    if (wait <= t->seq_pos)
        return 0;
    cseq.late[k] = wait - t->seq_pos;
    cseq.late_idx[k] = (uint8_t)idx;
    cseq.waiting |= (uint8_t)(1u << k);
    return 1;
}

/* chord_strum_next: the next string, late or early entry within limit */
static uint32_t cseq_next(uint32_t limit)
{
    uint32_t k, j, due, ahead, cur;
    if (!song.playing)
        return limit;
    for (k = 0; k < trk_n(); k++) {
        const track_t *t = &trk[k];
        if (t->seq_pos >= 0x7FFFFFFFu)
            continue;
        if ((cseq.strumming >> k) & 1u) {
            due = chord_strum_due(&cseq.strum[k], cseq.started[k], 0);
            if (due && due < limit)
                limit = due;
        }
        if (((cseq.waiting >> k) & 1u) && cseq.late[k] && cseq.late[k] < limit)
            limit = cseq.late[k];
        ahead = cseq_early(t, &j);
        cur = step_samples(t, seq_div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
        if (ahead && cur > t->seq_pos + ahead && cur - t->seq_pos - ahead < limit)
            limit = cur - t->seq_pos - ahead;
    }
    return limit;
}

/* key_on, on the track SEQ > CHORDS shows: the chord a new entry gets, swept as it would be -> the notes to start
 * now; 0: not so (chord keys as SCL > CHORD sets them) */
static uint32_t cseq_key(track_t *t, uint32_t root, uint8_t *out, chord_strum_t *s)
{
    cseq_t c = cseq.pen;
    if (cseq.live != trk_index(t) + 1u)
        return 0;
    return chord_strum_begin_as(t, out, cseq_chord(t, root, &c, out), s, 100, c.v[CS_STRUM], c.v[CS_DIR], 0);
}
