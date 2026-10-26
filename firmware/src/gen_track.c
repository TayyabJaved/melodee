/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* ARP > GEN: a part plays one of Tessera's generators (gen.c: TURING, EUCLID, MARKOV, ATLAS, SERIES, BOUNCE) instead
 * of its steps.
 * Each step the generator makes a step that seq_step plays as a written one (SWING, the voices, MIDI OUT, the DRUM
 * lanes); the written steps stay and play again with MODE OFF. A note: the generator's, on the track's key, snapped
 * down to its scale, TRN added, for GATE (PAT: the PATTERN's; HOLD: to its next note); ATLAS: kick, snare and closed hat
 * on their DRUM lanes, on any engine.
 * GEN: MODE, SEED, CHANGE, EVERY. The seed (1..GN_SEED_MAX) starts the random stream and material, again at every PLAY,
 * so a project plays the same each time. CHANGE n is n small changes, each one worked out from the seed and its own
 * number alone (gn_changes): turning CHANGE back gives the earlier version exactly. They are offsets on the settings
 * (TURING: flipped bits of its loop; EUCLID: its rotation, its pitches drawn again; MARKOV: LEAP; ATLAS: X and Y;
 * SERIES: START; BOUNCE: ANGLE), so
 * GEN 2's knobs keep what was set. Playing, a new SEED or CHANGE starts on the track's next bar. EVERY turns CHANGE up
 * one every so many bars. GEN 2: the mode's three settings and GATE (MARKOV: LOCK; ATLAS: CHAOS).
 * MARKOV's LOCK (Tessera's): its first pass over PATTERN's LEN (32 steps at most) is kept; after it each step plays the
 * kept one at LOCK % (100: a loop), else walks on. Kept again from the next loop start when the line changes (gn_rearm);
 * never saved: PLAY starts from the seed, so the first pass is the same again.
 * Print: the notes also go into Capture's ring (cap_ev), so REC held writes the last bars into the pattern. Saved in
 * the project's 'G' block (project.c). Included by seq.c */
#include "gen.h"
#include "gen.c"

enum { GN_OFF, GN_TURING, GN_EUCLID, GN_MARKOV, GN_ATLAS, GN_SERIES, GN_BOUNCE, GN_MODES };   /* (saved: append only) */
enum { GN_PAT, GN_HOLD = 6, GN_GATES };                /* GATE: PAT, 10 25 50 75 100 %, HOLD */
#define GN_SEED_MAX 128u
#define GN_CHANGE_MAX 99u
#define GN_PHRASE 32u                                   /* MARKOV's kept phrase, steps */
static const char *const GN_NAME[GN_MODES] = {"OFF", "TURING", "EUCLID", "MARKOV", "ATLAS", "SERIES", "BOUNCE"};
static const char *const GN_EVERY_N[] = {"OFF", "1", "2", "4", "8", "16"};
static const char *const GN_GATE_N[GN_GATES] = {"PAT", "10%", "25%", "50%", "75%", "FULL", "HOLD"};
static const uint8_t GN_EVERY_BARS[] = {0, 1, 2, 4, 8, 16};
static const uint8_t GN_GATE_PCT[GN_HOLD] = {0, 10, 25, 50, 75, 100};
static const uint8_t GN_LANE[GEN_GRIDS_PARTS] = {0, 1, 3, 5};   /* ATLAS's parts: DRUM_LANE_NOTE 36 38 42 45 */
static struct {
    uint8_t mode, seed, change, every, gate, lock;   /* the knobs (SEED, CHANGE: as set, maybe waiting for the bar) */
    uint8_t aseed, achange;                   /* the seed and CHANGE playing */
    uint8_t last, held;                       /* the last note (the picture); HOLD's note still open in Capture, 0 none */
    uint8_t bars;                             /* bars since EVERY last turned CHANGE */
    uint16_t pos;                             /* steps since PLAY or the seed */
    uint32_t rnd;
    uint16_t t_mask;                          /* CHANGE's offsets (gn_changes) */
    int8_t e_rot, m_leap, a_x, a_y, s_start, b_angle;
    uint8_t e_pitch;
    uint8_t rec, lp, lp_len;                  /* MARKOV's phrase: 1 keeping it, 2 from the next loop start, 0 kept; */
    uint8_t ph_note[GN_PHRASE], ph_vel[GN_PHRASE];   /* its step, its length; each step: bit 7 a note, its note; vel */
    gen_turing_t turing;                      /* the settings as set, the playheads */
    gen_euclid_t euclid;
    gen_markov_t markov;
    gen_grids_t grids;
    gen_series_t series;
    gen_bounce_t bounce;
} gn[GN_TRACKS] __attribute__((section(".pool")));

static void seq_release(track_t *t);

static void gn_defaults(uint32_t k)
{
    memset(&gn[k], 0, sizeof gn[k]);
    gn[k].turing.len = 16; gn[k].turing.oct_span = 2;
    gn[k].euclid.steps = 16; gn[k].euclid.pulses = 4;
    gn[k].markov.tension = 25; gn[k].markov.density = 70; gn[k].markov.oct_span = 2;
    gn[k].grids.x = gn[k].grids.y = 128; gn[k].grids.dens = 50;
    gn[k].series.intv = 1;
    gn[k].bounce.speed = 24; gn[k].bounce.angle = 37; gn[k].bounce.balls = 1;
}
static void gn_ready(uint32_t k) { if (!gn[k].turing.len) gn_defaults(k); }   /* (power-on: zero) */
static void gn_seed_fresh(uint32_t k)                  /* a track's first seed: one of its own */
{
    if (!gn[k].seed) gn[k].seed = (uint8_t)(1u + gen_stream(rng() ^ fm1_ms ^ k * 0x9E3779B9u) % GN_SEED_MAX);
}

/* CHANGE's offsets for the seed and CHANGE playing: change i from (seed, i) alone, so CHANGE n is always the same */
static void gn_changes(uint32_t k)
{
    uint32_t i, sd = gn[k].aseed;
    int32_t rot = 0, leap = 0, x = 0, y = 0, start = 0, angle = 0;
    gn[k].t_mask = 0;
    gn[k].e_pitch = (uint8_t)(1u + gen_stream(sd * 31u + 7u) % 255u);
    for (i = 1; i <= gn[k].achange; i++) {
        uint32_t r = gen_stream(sd * 977u + i * 0x9E3779B9u), q = gen_stream(r);
        gn[k].t_mask ^= (uint16_t)(1u << (r & 15u));                   /* TURING: a bit of its loop */
        if ((r >> 4) % 3u) rot += (r >> 6) & 1u ? 1 : -1;               /* EUCLID: a step round, or its pitches */
        else gn[k].e_pitch = (uint8_t)(1u + (r >> 8) % 255u);
        leap += (int32_t)((r >> 16) % 21u) - 10;                       /* MARKOV: LEAP up to +-10 */
        x += (int32_t)(q % 33u) - 16;                                  /* ATLAS: up to 16 across the map */
        y += (int32_t)((q >> 8) % 33u) - 16;
        start += (int32_t)(1u + ((q >> 16) & 7u)) * ((q >> 19) & 1u ? 1 : -1);   /* SERIES: START 1..8 on */
        angle += (int32_t)((q >> 20) % 11u) - 5;                       /* BOUNCE: ANGLE up to 5 degrees */
    }
    gn[k].e_rot = (int8_t)(rot % 32);
    gn[k].m_leap = (int8_t)clamp(leap, -60, 60);
    gn[k].a_x = (int8_t)clamp(x, -96, 96);
    gn[k].a_y = (int8_t)clamp(y, -96, 96);
    gn[k].s_start = (int8_t)(start % 128);
    gn[k].b_angle = (int8_t)clamp(angle, -45, 45);
}
static void gn_rearm(uint32_t k) { if (gn[k].rec != 1u || gn[k].lp) gn[k].rec = 2; }   /* MARKOV: keep it again */
/* the CHANGE set now playing: its offsets, TURING's loop from the seed's with its bits flipped */
static void gn_change_now(uint32_t k)
{
    gn_rearm(k);
    uint16_t reg;
    gn[k].achange = gn[k].change;
    gn_changes(k);
    reg = (uint16_t)(gen_turing_pick(gn[k].aseed) ^ gn[k].t_mask);
    gn[k].turing.reg = reg ? reg : 1u;
    gn[k].turing.step_idx = 0xFFu;                      /* (the loop's first step: the next one, GEN's display) */
}
/* TURING's loop as GEN shows it, from the step sounding (0) on: is step j a hit (bit 0 sounds, bit LEN-1 comes next,
 * then LEN-2 ..); which of them is the loop's first step */
static uint32_t gn_turing_hit(uint32_t k, uint32_t j)
{
    uint32_t len = gn[k].turing.len ? gn[k].turing.len : 16u;
    return (gn[k].turing.reg >> ((len - j % len) % len)) & 1u;
}
static uint32_t gn_turing_first(uint32_t k)
{
    uint32_t len = gn[k].turing.len ? gn[k].turing.len : 16u;
    return (len - (gn[k].turing.step_idx < len ? gn[k].turing.step_idx : len - 1u)) % len;
}
/* back to the seed's start: its stream, the CHANGE set, every playhead (PLAY, a new seed) */
static void gn_restart(uint32_t k)
{
    gn_ready(k);
    gn_seed_fresh(k);
    gn[k].aseed = gn[k].seed;
    gn[k].rnd = gen_stream(gn[k].aseed);
    gn_change_now(k);
    gn[k].euclid.step_idx = 0;
    gn[k].markov.cur_state = 0;
    gn[k].grids.step_idx = 0;
    memset(gn[k].grids.perturb, 0, sizeof gn[k].grids.perturb);
    gen_series_start(&gn[k].series);
    gen_bounce_serve(&gn[k].bounce, gn[k].aseed);      /* (the extra balls and the leaves: the seed's) */
    gn[k].pos = 0;
    gn[k].bars = 0;
    gn[k].last = gn[k].held = 0;
    gn[k].rec = 1; gn[k].lp = 0;
}
/* gn_restart_all and gn_next stay in flash (tools/audio_ram.py XIP): the audio code calls them through a pointer, so
 * the generators cost the audio arena no resident code */
static __attribute__((noinline)) void gn_restart_all(void) { for (uint32_t k = 0; k < GN_TRACKS; k++) if (gn[k].mode) gn_restart(k); }

static int gn_active(const track_t *t) { uint32_t k = trk_index(t); return k < GN_TRACKS && gn[k].mode; }
static uint32_t gn_gate(uint32_t k) { return gn[k].mode == GN_MARKOV ? GN_PAT : gn[k].gate; }   /* (MARKOV: LOCK) */
/* GATE HOLD: its note held to the next step, where a rest ties it on (seq_step) */
static int gn_holds(const track_t *t)
{
    uint32_t k = trk_index(t);
    return k < GN_TRACKS && gn[k].mode && gn[k].mode != GN_ATLAS && gn_gate(k) == GN_HOLD;
}

/* (with the audio interrupts paused: ui_popup.c gn_mode_set, gn_page_put) */
static void gn_mode_apply(uint32_t k, uint32_t mode)
{
    if (k >= GN_TRACKS || mode >= GN_MODES || mode == gn[k].mode) return;
    gn_ready(k);
    seq_release(&trk[k]);                               /* (what the old source sounds) */
    gn[k].mode = (uint8_t)mode;
    if (mode) gn_restart(k);
}
/* SEED, CHANGE: at once when stopped, else on the track's next bar (gn_step) */
static void gn_seed_apply(uint32_t k, uint32_t seed)
{
    if (k >= GN_TRACKS || !seed || seed > GN_SEED_MAX) return;
    gn[k].seed = (uint8_t)seed;
    if (!song.playing || !gn[k].mode) gn_restart(k);
}
static void gn_change_apply(uint32_t k, uint32_t change)
{
    if (k >= GN_TRACKS || change > GN_CHANGE_MAX) return;
    gn[k].change = (uint8_t)change;
    if (!song.playing || !gn[k].mode) { gn_seed_fresh(k); gn[k].aseed = gn[k].seed; gn_change_now(k); }
}

/* the settings as they play: CHANGE's offsets on the knobs' values (the playheads are the knobs' copies') */
static void gn_eff_euclid(uint32_t k, gen_euclid_t *e)
{
    uint32_t n = gn[k].euclid.steps ? gn[k].euclid.steps : 16u;
    *e = gn[k].euclid;
    e->rot = (uint8_t)(((int32_t)e->rot + gn[k].e_rot % (int32_t)n + (int32_t)n) % (int32_t)n);
    e->pitch_seed = gn[k].e_pitch;
}
static void gn_eff_markov(uint32_t k, gen_markov_t *m)
{
    *m = gn[k].markov;
    m->tension = (uint8_t)clamp((int32_t)m->tension + gn[k].m_leap, 0, 100);
}
static void gn_eff_grids(uint32_t k, gen_grids_t *g)
{
    *g = gn[k].grids;
    g->x = (uint8_t)clamp((int32_t)g->x + gn[k].a_x, 0, 255);
    g->y = (uint8_t)clamp((int32_t)g->y + gn[k].a_y, 0, 255);
}

static void gn_eff_series(uint32_t k, gen_series_t *s)
{
    *s = gn[k].series;
    s->start = (uint8_t)(((int32_t)s->start + gn[k].s_start + 128) % 128);
}
static uint32_t gn_eff_angle(uint32_t k) { return (uint32_t)clamp((int32_t)gn[k].bounce.angle + gn[k].b_angle, 0, 90); }

/* steps in a 4/4 bar at the track's DIV */
static uint32_t gn_steps_per_bar(const track_t *t)
{
    uint32_t q = seq_div_samples(0u), d = seq_div_samples((uint32_t)t->p[P_SDIV]), n = d ? (4u * q + d / 2u) / d : 16u;
    return n ? n : 1u;
}
static int32_t gn_snap(const track_t *t, int32_t n)    /* down to the track's scale */
{
    uint32_t mask = scale_mask(t) & 0xFFFu, guard = 12;
    if (!mask) return n;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}
/* into Capture's ring: dur in 1/256 of a step, 0 still held (closed by cap_off) */
static void gn_cap(const track_t *t, uint32_t note, uint32_t vel, uint32_t dur)
{
    uint32_t w = cap_w;
    cap_on(t, note, vel);
    if (cap_w != w && dur) {
        cap_ev[w % CAP_N].dur = (uint16_t)dur;
        cap_ev[w % CAP_N].tr &= 127u;
    }
}

/* MARKOV's LOCK on its step out: the first pass kept, later ones the kept step at LOCK % (Tessera's
 * tessera_phrase_apply; a draw only between 0 and 100) */
static void gn_phrase(uint32_t k, const track_t *t, gen_out_t *out)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, GN_PHRASE), p;
    if (len != gn[k].lp_len) { gn[k].lp_len = (uint8_t)len; gn[k].lp = 0; gn_rearm(k); }
    p = gn[k].lp % len;
    if (gn[k].rec == 2u && !p) gn[k].rec = 1;
    if (gn[k].rec == 1u) {
        gn[k].ph_note[p] = (uint8_t)((out->gate ? 0x80u : 0u) | (out->note & 0x7Fu));
        gn[k].ph_vel[p] = out->vel;
    } else if (!gn[k].rec && gn[k].lock && (gn[k].lock >= 100u || gen_rnd(&gn[k].rnd, 100u) < gn[k].lock)) {
        out->gate = (uint8_t)(gn[k].ph_note[p] >> 7);
        out->note = (uint8_t)(gn[k].ph_note[p] & 0x7Fu);
        out->vel = gn[k].ph_vel[p];
    }
    gn[k].lp = (uint8_t)(p + 1u >= len ? 0u : p + 1u);
    if (!gn[k].lp && gn[k].rec == 1u) gn[k].rec = 0;
}

/* a bar line of track k's own count: EVERY's turn of CHANGE, then a SEED or CHANGE waiting */
static void gn_bar(uint32_t k)
{
    if (gn[k].pos && gn[k].every && ++gn[k].bars >= GN_EVERY_BARS[gn[k].every]) {
        gn[k].bars = 0;
        if (gn[k].change < GN_CHANGE_MAX) gn[k].change++;
    }
    if (gn[k].seed != gn[k].aseed) gn_restart(k);
    else if (gn[k].change != gn[k].achange) gn_change_now(k);
}

/* track k's generator step into *s */
static __attribute__((noinline)) const step_t *gn_next(track_t *t, step_t *s)
{
    uint32_t k = trk_index(t), spb, i, dur;
    gen_out_t out = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
    gen_clock_t clk;
    spb = gn_steps_per_bar(t);
    if (!(gn[k].pos % spb)) gn_bar(k);
    clk.pos = (uint8_t)gn[k].pos;
    clk.steps_per_bar = (uint8_t)(spb > 255u ? 255u : spb);
    clk.root = (uint8_t)(t->p[P_ROOT] % 12);
    switch (gn[k].mode) {
    case GN_TURING: {
        gen_turing_t *g = &gn[k].turing;
        gen_turing_tick(g, &clk, &gn[k].rnd, &out);
        g->step_idx = g->step_idx + 1u >= (g->len ? g->len : 16u) ? 0 : (uint8_t)(g->step_idx + 1u);   /* (its loop position) */
        break;
    }
    case GN_EUCLID: {
        gen_euclid_t e;
        gn_eff_euclid(k, &e);
        gen_euclid_tick(&e, &clk, &gn[k].rnd, &out);
        gn[k].euclid.step_idx = e.step_idx;
        break;
    }
    case GN_MARKOV: {
        gen_markov_t m;
        gn_eff_markov(k, &m);
        gen_markov_tick(&m, &clk, &gn[k].rnd, &out);
        gn[k].markov.cur_state = m.cur_state;
        gn_phrase(k, t, &out);
        break;
    }
    case GN_SERIES: {
        gen_series_t sr;
        gn_eff_series(k, &sr);
        gen_series_tick(&sr, &out);
        gn[k].series.n = sr.n;
        break;
    }
    case GN_BOUNCE: {
        uint8_t angle = gn[k].bounce.angle;
        gn[k].bounce.angle = (uint8_t)gn_eff_angle(k);
        gen_bounce_tick(&gn[k].bounce, &clk, &gn[k].rnd, &out);
        gn[k].bounce.angle = angle;
        break;
    }
    default: {
        gen_grids_t g;
        gn_eff_grids(k, &g);
        gen_grids_tick(&g, &clk, &gn[k].rnd, &out);
        gn[k].grids.step_idx = g.step_idx;
        memcpy(gn[k].grids.perturb, g.perturb, sizeof g.perturb);
        break;
    }
    }
    gn[k].pos++;
    memset(s, 0, sizeof *s);
    if (!out.gate) {
        s->time = gn[k].mode != GN_ATLAS && gn_gate(k) == GN_HOLD && t->seq_n ? ST_TIE : ST_REST;   /* (HOLD: on) */
        return s;
    }
    s->time = ST_NOTE;
    dur = (uint32_t)clamp(t->p[P_SGATE], 1, 127) * 2u;
    if (gn[k].mode == GN_ATLAS) {
        s->vel = 96;
        for (i = 0; i < GEN_GRIDS_PARTS; i++)
            if ((out.parts >> i) & 1u) {
                s->hit |= (uint8_t)(1u << GN_LANE[i]);
                if ((out.accent >> i) & 1u) s->acc |= (uint8_t)(1u << GN_LANE[i]);
                gn_cap(t, DRUM_LANE_NOTE[GN_LANE[i]], (out.accent >> i) & 1u ? 127u : 96u, dur);
            }
        gn[k].last = DRUM_LANE_NOTE[GN_LANE[__builtin_ctz(out.parts)]];
        return s;
    }
    {
        int32_t key = gn[k].mode == GN_MARKOV ? 0 : t->p[P_ROOT] % 12;   /* (MARKOV: its own key) */
        s->note[0] = (uint8_t)clamp(gn_snap(t, (int32_t)out.note + key) + t->p[P_TRANS], 0, 127);
        s->n = 1;
        for (i = 0; i < out.more_n && i < 2u; i++) {    /* (BOUNCE: balls hitting on the same step, three at most) */
            uint32_t j, x = (uint32_t)clamp(gn_snap(t, (int32_t)out.more[i] + key) + t->p[P_TRANS], 0, 127);
            for (j = 0; j < s->n && s->note[j] != x; j++)
                ;
            if (j == s->n) s->note[s->n++] = (uint8_t)x;
        }
        s->vel = out.vel;
        uint32_t gate = gn_gate(k);
        if (gate && gate < GN_HOLD) {                   /* a share of the step, as a recorded gate (seq_step) */
            s->acc = (uint8_t)(GN_GATE_PCT[gate] * 255u / 100u);
            dur = GN_GATE_PCT[gate] * 256u / 100u;
        } else if (gate == GN_HOLD) {
            s->acc = 255;
            dur = 0;
        }
        if (gn[k].held) cap_off(t, gn[k].held);
        gn[k].held = dur ? 0u : s->note[0];
        gn[k].last = s->note[0];
        for (i = 0; i < s->n; i++) gn_cap(t, s->note[i], s->vel, dur);
    }
    return s;
}
/* track t's step: its generator's into *s, the written one with MODE OFF */
static const step_t *gn_step(track_t *t, const step_t *written, step_t *s)
{
    uint32_t k = trk_index(t);
    return k < GN_TRACKS && gn[k].mode ? gn_next(t, s) : written;
}

/* the project's 'G' block (version 4): a track's GN_SAVE bytes; version 1 had the first 14, 2 the first 18, 3 19 */
static void gn_save(uint32_t k, uint8_t *b)
{
    gn_ready(k);
    b[0] = gn[k].mode; b[1] = gn[k].seed;
    b[2] = gn[k].turing.len; b[3] = gn[k].turing.prob; b[4] = gn[k].turing.oct_span;
    b[5] = gn[k].euclid.steps; b[6] = gn[k].euclid.pulses; b[7] = gn[k].euclid.rot;
    b[8] = gn[k].markov.tension; b[9] = gn[k].markov.density; b[10] = gn[k].markov.oct_span;
    b[11] = gn[k].grids.x; b[12] = gn[k].grids.y; b[13] = gn[k].grids.dens;
    b[14] = gn[k].change; b[15] = gn[k].every; b[16] = gn[k].gate; b[17] = gn[k].grids.chaos; b[18] = gn[k].lock;
    b[19] = gn[k].series.start; b[20] = gn[k].series.intv; b[21] = gn[k].series.loop;
    b[22] = gn[k].bounce.speed; b[23] = gn[k].bounce.angle; b[24] = gn[k].bounce.balls;
}
/* every value back inside its range; none saved (an older project: zero), the defaults with MODE OFF */
static void gn_load(uint32_t k, const uint8_t *b)
{
    gn_defaults(k);
    if (!b[2]) return;
    gn[k].mode = (uint8_t)(b[0] < GN_MODES ? b[0] : GN_OFF);
    gn[k].seed = (uint8_t)(b[1] <= GN_SEED_MAX ? b[1] : 0u);
    gn[k].turing.len = (uint8_t)clamp(b[2], 2, 16);
    gn[k].turing.prob = (uint8_t)clamp(b[3], 0, 100);
    gn[k].turing.oct_span = (uint8_t)clamp(b[4], 1, 3);
    gn[k].euclid.steps = (uint8_t)clamp(b[5], 1, 32);
    gn[k].euclid.pulses = (uint8_t)clamp(b[6], 0, gn[k].euclid.steps);
    gn[k].euclid.rot = (uint8_t)clamp(b[7], 0, gn[k].euclid.steps - 1);
    gn[k].markov.tension = (uint8_t)clamp(b[8], 0, 100);
    gn[k].markov.density = (uint8_t)clamp(b[9], 5, 100);
    gn[k].markov.oct_span = (uint8_t)clamp(b[10], 1, 3);
    gn[k].grids.x = b[11]; gn[k].grids.y = b[12];
    gn[k].grids.dens = (uint8_t)clamp(b[13], 0, 100);
    gn[k].change = (uint8_t)clamp(b[14], 0, GN_CHANGE_MAX);
    gn[k].every = (uint8_t)clamp(b[15], 0, (int32_t)NELEM(GN_EVERY_BARS) - 1);
    gn[k].gate = (uint8_t)clamp(b[16], 0, GN_GATES - 1);
    gn[k].grids.chaos = (uint8_t)clamp(b[17], 0, 100);
    gn[k].lock = (uint8_t)clamp(b[18], 0, 100);
    if (b[20]) {                                        /* (version 4: SERIES, BOUNCE; before: their defaults) */
        gn[k].series.start = (uint8_t)(b[19] & 127u);
        gn[k].series.intv = (uint8_t)clamp(b[20], 1, 7);
        gn[k].series.loop = (uint8_t)clamp(b[21], 0, GEN_SR_LOOP);
        gn[k].bounce.speed = (uint8_t)clamp(b[22], 1, 64);
        gn[k].bounce.angle = (uint8_t)clamp(b[23], 0, 90);
        gn[k].bounce.balls = (uint8_t)clamp(b[24], 1, GEN_BOUNCE_FALL);
    }
    if (gn[k].mode) gn_restart(k);
}

/* GEN's knobs (ids 0..3: MODE SEED CHANGE EVERY) and GEN 2's (4..7: the mode's three settings, GATE; MARKOV: LOCK;
 * ATLAS: CHAOS)
 * (params.c page_desc, ui_input.c edit_param); a copy of the value in gn_cell, written back by ui_popup.c gn_page_put */
static const param_desc_t GN_MODE_D = PE("MODE", GN_NAME, 0);
static const param_desc_t GN_COMMON_D[3] = {
    PD("SEED", F_INT, 1, GN_SEED_MAX, 1), PD("CHANGE", F_INT, 0, GN_CHANGE_MAX, 0),
    PE("EVERY", GN_EVERY_N, 0)};                        /* (bars: the unit would run off the screen) */
static const param_desc_t GN_GATE_D = PE("GATE", GN_GATE_N, 0);
static const param_desc_t GN_CHAOS_D = PD("CHAOS", F_PCT, 0, 100, 0);
static const param_desc_t GN_LOCK_D = PD("LOCK", F_PCT, 0, 100, 0);
static const char *const GN_LOOP_N[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                        "15", "16"};
static const char *const GN_BALLS_N[] = {"1", "2", "3", "4", "FALL"};
static const param_desc_t GN_WIND_D = PD("WIND", F_INT, 0, 90, 37);   /* (FALL: ANGLE, the wind round its middle) */
static const param_desc_t GN_D[GN_MODES - 1u][3] = {
    {PD("LEN", F_INT, 2, 16, 16), PD("PROB", F_PCT, 0, 100, 0), {"SPAN", F_INT, 1, 3, 2, 0, "oct"}},
    {PD("STEPS", F_INT, 1, 32, 16), PD("PULSE", F_INT, 0, 32, 4), PD("ROT", F_INT, 0, 31, 0)},
    {PD("LEAP", F_PCT, 0, 100, 25), PD("DENS", F_PCT, 5, 100, 70), {"SPAN", F_INT, 1, 3, 2, 0, "oct"}},
    {PD("X", F_INT, 0, 255, 128), PD("Y", F_INT, 0, 255, 128), PD("DENS", F_PCT, 0, 100, 50)},
    {PD("START", F_INT, 0, 127, 0), PD("INTV", F_INT, 1, 7, 1), PE("LOOP", GN_LOOP_N, 0)},
    {PD("SPEED", F_INT, 1, 64, 24), PD("ANGLE", F_INT, 0, 90, 37), PE("BALLS", GN_BALLS_N, 0)},   /* (BALLS: 1..5, a cell 0..4) */
};
static int16_t gn_cell[4] __attribute__((section(".pool")));
static uint8_t *gn_value(uint32_t k, uint32_t id)      /* knob id's setting (the MODE playing), 0 none */
{
    switch (id) {
    case 1: return &gn[k].seed;
    case 2: return &gn[k].change;
    case 3: return &gn[k].every;
    case 7: return gn[k].mode == GN_ATLAS ? &gn[k].grids.chaos : gn[k].mode == GN_MARKOV ? &gn[k].lock : &gn[k].gate;
    }
    switch (gn[k].mode) {
    case GN_TURING: return id == 4u ? &gn[k].turing.len : id == 5u ? &gn[k].turing.prob : &gn[k].turing.oct_span;
    case GN_EUCLID: return id == 4u ? &gn[k].euclid.steps : id == 5u ? &gn[k].euclid.pulses : &gn[k].euclid.rot;
    case GN_MARKOV: return id == 4u ? &gn[k].markov.tension : id == 5u ? &gn[k].markov.density : &gn[k].markov.oct_span;
    case GN_ATLAS: return id == 4u ? &gn[k].grids.x : id == 5u ? &gn[k].grids.y : &gn[k].grids.dens;
    case GN_SERIES: return id == 4u ? &gn[k].series.start : id == 5u ? &gn[k].series.intv : &gn[k].series.loop;
    case GN_BOUNCE: return id == 4u ? &gn[k].bounce.speed : id == 5u ? &gn[k].bounce.angle : &gn[k].bounce.balls;
    }
    return 0;
}
static const param_desc_t *gn_page_desc(uint32_t id, int16_t **valp)
{
    uint32_t k = song.sel;
    uint8_t *v;
    *valp = 0;
    if (k >= GN_TRACKS || id > 7u) return 0;
    gn_ready(k);
    if (!id) {
        gn_cell[0] = gn[k].mode;
        *valp = &gn_cell[0];
        return &GN_MODE_D;
    }
    if (!gn[k].mode) return 0;                          /* (OFF: MODE alone) */
    gn_seed_fresh(k);
    if (!(v = gn_value(k, id))) return 0;
    gn_cell[id & 3u] = (int16_t)(*v - (gn[k].mode == GN_BOUNCE && id == 6u ? 1 : 0));   /* (BALLS 1..5: the names') */
    *valp = &gn_cell[id & 3u];
    if (gn[k].mode == GN_BOUNCE && id == 5u && gn[k].bounce.balls == GEN_BOUNCE_FALL) return &GN_WIND_D;
    return id <= 3u ? &GN_COMMON_D[id - 1u] :
           id == 7u ? (gn[k].mode == GN_ATLAS ? &GN_CHAOS_D : gn[k].mode == GN_MARKOV ? &GN_LOCK_D : &GN_GATE_D) :
           &GN_D[gn[k].mode - 1u][id - 4u];
}
