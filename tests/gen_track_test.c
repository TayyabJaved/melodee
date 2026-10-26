/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* ARP > GEN (firmware/src/gen_track.c) against the real sequencer, UI, Capture and project code. MODE OFF plays the
 * written steps; a generator replaces them with notes in the track's scale, TRN added, the same again from its seed at
 * every PLAY; EUCLID plays PULSE hits a cycle; ATLAS plays kick, snare and hat on the DRUM lanes; a new seed waits for
 * the track's next bar while playing; REC does not arm a generator track and REC held prints (the notes into the
 * pattern, MODE OFF); the project's 'G' block round trips, is written only while a generator plays, clamps or ignores a
 * damaged one, moves with a removed track; a template loads OFF; GEN shows on the parts only, KNOB 1 its MODE. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t period(const track_t *t) { return seq_div_samples((uint32_t)t->p[P_SDIV]); }
/* n steps of track k from PLAY: note[i] the step's first note (0 a rest), cnt[i] how many it played */
static void play_steps(uint32_t k, uint32_t n, uint8_t *note, uint8_t *cnt, uint8_t *notes4)
{
    track_t *t = &trk[k];
    seq_start();
    seq_advance(1);
    for (uint32_t i = 0; i < n; i++) {
        note[i] = t->seq_n ? t->seq_notes[0] : 0;
        if (cnt) cnt[i] = t->seq_n;
        if (notes4) for (uint32_t j = 0; j < 4u; j++) notes4[i * 4u + j] = j < t->seq_n ? t->seq_notes[j] : 0;
        seq_advance(period(t));
    }
    seq_stop();
}
static void reset(void)
{
    ui_power_on();
    set_engine_of(&trk[0], HOST_SYNTH);
    trk[0].p[P_SCALE] = 1; trk[0].p[P_ROOT] = 0; trk[0].p[P_VOICE] = V_POLY;   /* C major */
    for (uint32_t k = 0; k < GN_TRACKS; k++) gn_defaults(k);
    for (uint32_t i = 0; i < NSTEP; i++) { step_clear(&trk[0].step[i]); trk[0].step[i].n = 1; trk[0].step[i].note[0] = 100; trk[0].step[i].time = ST_NOTE; }
    trk[0].p[P_SLEN] = 16;
    pattern_commit(&trk[0]);
}

static int playback(void)
{
    int bad = 0;
    uint8_t a[64], b[32], c[32], cnt[64], n4[64 * 4];
    uint32_t i, ok, m;
    track_t *t = &trk[0];
    reset();
    play_steps(0, 16, a, 0, 0);
    for (i = 0, ok = 1; i < 16u; i++) ok &= a[i] == 100u;
    bad += check("MODE OFF: the written steps play", ok);

    gn_mode_apply(0, GN_TURING); gn[0].seed = 42; gn_restart(0);
    play_steps(0, 32, a, 0, 0);
    play_steps(0, 32, b, 0, 0);
    for (i = 0, ok = 1; i < 32u; i++) ok &= a[i] != 100u && (!a[i] || ((scale_mask(t) >> (a[i] % 12u)) & 1u));
    bad += check("TURING: the steps replaced, every note in C major", ok && memcmp(a, (uint8_t[32]){0}, 32));
    bad += check("  PLAY again: the same 32 steps from the seed", !memcmp(a, b, 32u));
    t->p[P_TRANS] = 2;
    play_steps(0, 32, c, 0, 0);
    for (i = 0, ok = 1; i < 32u; i++) ok &= (!a[i] && !c[i]) || c[i] == a[i] + 2u;
    bad += check("  TRN +2: the same notes two semitones up", ok);
    t->p[P_TRANS] = 0;
    gn_seed_apply(0, 43);
    play_steps(0, 32, c, 0, 0);
    bad += check("  another seed (stopped: at once): another line", memcmp(a, c, 32u) != 0);
    gn_seed_apply(0, 42);
    play_steps(0, 32, c, 0, 0);
    bad += check("  seed 42 again: the first line", !memcmp(a, c, 32u));

    gn_mode_apply(0, GN_EUCLID); gn[0].euclid.steps = 16; gn[0].euclid.pulses = 5;
    play_steps(0, 32, a, cnt, 0);
    for (i = 0, ok = 0; i < 32u; i++) ok += a[i] != 0;
    bad += check("EUCLID 5 of 16: 10 hits in two cycles", ok == 10u);

    gn_mode_apply(0, GN_ATLAS); gn[0].grids.dens = 70;
    play_steps(0, 16, a, cnt, n4);
    {
        uint32_t kick = 0, hat = 0, other = 0;
        for (i = 0; i < 16u * 4u; i++) {
            kick += n4[i] == 36u; hat += n4[i] == 42u;
            other += n4[i] && n4[i] != 36u && n4[i] != 38u && n4[i] != 42u;
        }
        bad += check("ATLAS: kick and closed hat on their DRUM lanes, nothing else", kick && hat && !other);
    }

    gn_mode_apply(0, GN_SERIES); gn[0].series.loop = 4;
    play_steps(0, 16, a, cnt, 0);
    for (i = 0, ok = 1; i < 16u; i++) ok &= cnt[i] && ((scale_mask(t) >> (a[i] % 12u)) & 1u) && a[i] == a[i % 4u];
    bad += check("SERIES LOOP 4: a note every step, in C major, every 4 steps the same", ok);
    gn_mode_apply(0, GN_BOUNCE); gn[0].bounce.balls = 4;
    play_steps(0, 64, a, cnt, n4);
    for (i = 0, ok = 1, m = 0; i < 64u; i++) {
        m += cnt[i] > 1u;
        for (uint32_t j = 0; j < cnt[i]; j++) ok &= (scale_mask(t) >> (n4[i * 4u + j] % 12u)) & 1u;
    }
    bad += check("BOUNCE 4 balls: wall hits play, some together, every note in C major", ok && m > 0u);
    gn[0].bounce.balls = GEN_BOUNCE_FALL;
    play_steps(0, 64, a, cnt, 0);
    for (i = 0, m = 0; i < 64u; i++) m += cnt[i] != 0;
    bad += check("  FALL: the leaves land and play", m > 4u);

    gn_mode_apply(0, GN_TURING);
    seq_start(); seq_advance(1);
    for (i = 0; i < 3u; i++) seq_advance(period(t));     /* (step 3 of the bar) */
    gn_seed_apply(0, 77);
    ok = gn[0].aseed == 42u && gn[0].seed == 77u;
    for (i = 0; i < 13u; i++) seq_advance(period(t));    /* (the next bar's first step) */
    ok &= gn[0].aseed == 77u;
    seq_stop();
    bad += check("a new seed while playing: waits for the track's next bar", ok);
    return bad;
}

/* GEN: CHANGE (each step one change, back again exactly), EVERY; GEN 2: GATE */
static int changes(void)
{
    int bad = 0;
    uint8_t a[32], b[32], c[32], cnt[32];
    uint32_t i, ok, m;
    track_t *t = &trk[0];
    for (m = GN_TURING; m <= GN_BOUNCE; m++) {
        uint8_t n4a[32 * 4], n4b[32 * 4], n4c[32 * 4];
        char what[96];
        reset();
        gn_mode_apply(0, m); gn_seed_apply(0, 42);
        if (m == GN_EUCLID) gn[0].euclid.pulses = 5;
        play_steps(0, 32, a, 0, n4a);
        gn_change_apply(0, 6);
        play_steps(0, 32, b, 0, n4b);
        gn_change_apply(0, 5); gn_change_apply(0, 0);
        play_steps(0, 32, c, 0, n4c);
        ok = memcmp(n4a, n4b, sizeof n4a) && !memcmp(n4a, n4c, sizeof n4a);
        gn_change_apply(0, 6);
        play_steps(0, 32, c, 0, n4c);
        ok &= !memcmp(n4b, n4c, sizeof n4b);
        snprintf(what, sizeof what, "CHANGE on %s: 6 changes another line, back to 0 the first, 6 again the same", GN_NAME[m]);
        bad += check(what, ok);
    }
    reset();
    gn_mode_apply(0, GN_EUCLID); gn_seed_apply(0, 9); gn[0].euclid.pulses = 5;
    ok = 1;
    for (i = 1; i <= 20u; i++) {
        gn_change_apply(0, i);
        ok &= gn[0].euclid.pulses == 5u && gn[0].euclid.rot == 0u && gn[0].euclid.steps == 16u;
    }
    bad += check("  CHANGE leaves GEN 2's knobs as they were set (offsets on top)", ok);
    gn_change_apply(0, 0);
    seq_start(); seq_advance(1);
    for (i = 0; i < 3u; i++) seq_advance(period(t));
    gn_change_apply(0, 4);
    ok = gn[0].achange == 0u && gn[0].change == 4u;
    for (i = 0; i < 13u; i++) seq_advance(period(t));
    ok &= gn[0].achange == 4u;
    seq_stop();
    bad += check("  CHANGE while playing: from the track's next bar", ok);
    gn_change_apply(0, 0); gn[0].every = 1;                /* (every bar) */
    seq_start(); seq_advance(1);
    for (i = 0; i < 16u * 4u; i++) seq_advance(period(t));
    ok = gn[0].change == 4u && gn[0].achange == 4u;
    seq_stop();
    gn[0].every = 0;
    bad += check("EVERY 1: CHANGE one up each bar (4 bars: +4), played from the bar", ok);

    reset();
    gn_mode_apply(0, GN_TURING); gn_seed_apply(0, 3); gn[0].turing.prob = 60;   /* (rests and notes) */
    gn[0].gate = 3;                                        /* 50 % */
    {
        step_t s;
        const step_t *r;
        for (i = 0, ok = 0; i < 16u; i++) { r = gn_step(t, &t->step[0], &s); if (r->n) { ok = r->acc == 127u; break; } }
    }
    bad += check("GATE 50 %: a note half a step long", ok);
    gn[0].gate = GN_PAT; gn_restart(0);
    play_steps(0, 32, a, cnt, 0);
    for (i = 0, m = 0; i < 32u; i++) m += !cnt[i];
    gn[0].gate = GN_HOLD;
    play_steps(0, 32, b, cnt, 0);
    for (i = 0, ok = 0; i < 32u && !cnt[i]; i++) ;
    for (ok = 1; i < 32u; i++) ok &= cnt[i] != 0;
    bad += check("GATE HOLD: the rests that PAT has hold the last note on to the next", m > 0u && ok);

    /* MARKOV's LOCK: 4 passes of LEN 16 */
    {
        uint8_t p[64], q2[64];
        uint32_t same12, same23, kept, fresh;
        reset();
        gn_mode_apply(0, GN_MARKOV); gn_seed_apply(0, 3); gn[0].gate = GN_HOLD;
        gn[0].lock = 100; gn_restart(0);
        play_steps(0, 64, p, cnt, 0);
        bad += check("MARKOV LOCK 100: its first pass (LEN 16) again every pass", !memcmp(p, p + 16, 16) && !memcmp(p, p + 32, 32));
        for (i = 0, ok = 1; i < 64u; i++) ok &= !(p[i] && !cnt[i]) && (p[i] || !cnt[i]);
        play_steps(0, 64, q2, 0, 0);
        bad += check("  GATE ignored on MARKOV (HOLD set: no ties), the loop the same at the next PLAY", ok && !memcmp(p, q2, sizeof p));
        gn[0].lock = 0; gn_restart(0);
        play_steps(0, 64, q2, 0, 0);
        bad += check("  LOCK 0: the first pass as with 100, then walking on", !memcmp(p, q2, 16) && memcmp(q2, q2 + 16, 16));
        gn[0].lock = 50; gn_restart(0);
        play_steps(0, 64, q2, 0, 0);
        for (i = 16, kept = fresh = 0; i < 64u; i++) { kept += q2[i] == q2[i % 16u]; fresh += q2[i] != q2[i % 16u]; }
        bad += check("  LOCK 50: later passes partly the kept phrase, partly new", kept > 8u && fresh > 4u);
        gn[0].lock = 100; gn_restart(0);
        seq_start(); seq_advance(1);
        for (i = 0; i < 24u; i++) seq_advance(period(t));          /* (into pass 2: playing the kept phrase) */
        same12 = gn[0].rec == 0u;
        fm1_irq_off(); gn[0].markov.tension = 90; gn_rearm(0); fm1_irq_on();
        for (i = 0; i < 8u; i++) seq_advance(period(t));           /* (pass 3 starts: kept again) */
        same23 = gn[0].rec == 1u;
        for (i = 0; i < 16u; i++) seq_advance(period(t));
        same23 &= gn[0].rec == 0u;
        seq_stop();
        bad += check("  LEAP changed while playing: kept again from the next loop start", same12 && same23);
    }
    return bad;
}

static int rec_print(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint8_t played[128] = {0};
    uint32_t i, n = 0, ok = 1;
    reset();
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    pattern_commit(t);
    gn_mode_apply(0, GN_EUCLID); gn[0].seed = 5; gn_restart(0);
    song.sel = 0;
    rec_tap();
    bad += check("REC on a generator track: not armed, the hint", !(song.rec & 1u) && msg_is("GEN: HOLD REC TO PRINT"));
    seq_start(); seq_advance(1);
    for (i = 0; i < 32u; i++) {
        if (t->seq_n) played[t->seq_notes[0]] = 1;
        seq_advance(period(t));
    }
    gn_print();
    for (i = 0; i < RECORD_MAX; i++)
        if (recording_active(t, i)) { n++; ok &= played[recorded_note(&recording[i])]; }
    seq_stop();
    bad += check("Print: the generator's notes into the pattern as recorded notes, MODE OFF",
                 n > 0u && ok && !gn[0].mode && msg_is("PRINTED: GEN OFF"));
    gn_print();
    bad += check("  Print with MODE OFF: says so", msg_is("GEN IS OFF"));
    return bad;
}

static int storage(void)
{
    int bad = 0;
    static project_t q, q2;
    static project_store_t st, st2;
    uint32_t k;
    reset();
    gn_mode_apply(1, GN_EUCLID); gn[1].euclid.pulses = 5; gn[1].seed = 9; gn_restart(1);
    project_capture(&q);
    bad += check("capture: track 2's generator, the others none", q.gen[1][0] == GN_EUCLID && q.gen[1][1] == 9u && q.gen[1][6] == 5u &&
                 !q.gen[0][0] && !q.gen[2][0] && !q.gen[3][2]);
    bad += check("  packs and unpacks with it", proj_pack(&st, &q) && proj_unpack(&q2, (const uint8_t *)&st) && !memcmp(q.gen, q2.gen, sizeof q.gen));
    for (k = 0; k < GN_TRACKS; k++) gn_defaults(k);
    bad += check("  loads: EUCLID 5 of 16, seed 9", !project_restore_runtime(&q2) && gn[1].mode == GN_EUCLID && gn[1].euclid.pulses == 5u &&
                 gn[1].seed == 9u && !gn[0].mode);
    {
        uint8_t *raw = (uint8_t *)&st;
        uint32_t i, at = 0;
        for (i = 0; i + 2u < PROJ_MPC_OCT_WIRE; i++)
            if (raw[i] == 'G' && raw[i + 1u] == 4u && !memcmp(raw + i + 2u, q.gen, sizeof q.gen)) at = i;
        raw[at] = 0;
        uint32_t h = proj_hash(raw, PROJ_STORE_SIZE - 4u);
        memcpy(raw + PROJ_STORE_SIZE - 4u, &h, 4);
        memset(&q2, 0, sizeof q2);
        bad += check("  its tag gone (as older firmware sees it): the project loads, the generators OFF, the rest the same",
                     at && proj_unpack(&q2, raw) && !q2.gen[1][0] && !memcmp(q.t, q2.t, sizeof q.t) && !memcmp(q.chain.row, q2.chain.row, sizeof q.chain.row));
    }
    memset(q.gen, 0, sizeof q.gen); q.sum = proj_sum(&q);
    q2 = q; memset(q2.gen, 0xAA, sizeof q2.gen); for (k = 0; k < PROJ_TRK; k++) q2.gen[k][0] = 0; q2.sum = proj_sum(&q2);
    bad += check("no generator playing: no block, the record as before", proj_pack(&st, &q) && proj_pack(&st2, &q2) && !memcmp(&st, &st2, sizeof st));
    {
        const uint8_t bad_mode[GN_SAVE] = {9, 200, 0}, big[GN_SAVE] = {GN_TURING, 5, 99, 250, 9, 99, 99, 99, 250, 0, 0, 1, 2, 250};
        gn_load(0, bad_mode);
        bad += check("a damaged block: no LEN, the defaults and OFF", !gn[0].mode && gn[0].turing.len == 16u);
        gn_load(0, big);
        bad += check("  out of range: every value clamped", gn[0].mode == GN_TURING && gn[0].turing.len == 16u && gn[0].turing.prob == 100u &&
                     gn[0].turing.oct_span == 3u && gn[0].euclid.steps == 32u && gn[0].euclid.pulses == 32u && gn[0].euclid.rot == 31u &&
                     gn[0].markov.density == 5u && gn[0].markov.oct_span == 1u && gn[0].grids.dens == 100u);
    }
    {
        uint8_t v[GN_SAVE];
        gn_defaults(1); gn[1].mode = GN_ATLAS; gn[1].seed = 3; gn[1].change = 12; gn[1].every = 3; gn[1].gate = 4; gn[1].grids.chaos = 40;
        gn[1].lock = 70;
        gn[1].series.start = 33; gn[1].series.intv = 3; gn[1].series.loop = 9;
        gn[1].bounce.speed = 50; gn[1].bounce.angle = 12; gn[1].bounce.balls = GEN_BOUNCE_FALL;
        gn_save(1, v); gn_defaults(1); gn_load(1, v);
        bad += check("CHANGE, EVERY, GATE, CHAOS, LOCK, SERIES and BOUNCE saved and loaded", gn[1].mode == GN_ATLAS && gn[1].change == 12u && gn[1].achange == 12u &&
                     gn[1].every == 3u && gn[1].gate == 4u && gn[1].grids.chaos == 40u && gn[1].lock == 70u &&
                     gn[1].series.start == 33u && gn[1].series.intv == 3u && gn[1].series.loop == 9u &&
                     gn[1].bounce.speed == 50u && gn[1].bounce.angle == 12u && gn[1].bounce.balls == GEN_BOUNCE_FALL);
    }
    {   /* a version 1 block (14 bytes a track) */
        uint8_t *raw = (uint8_t *)&st;
        uint32_t i, at = 0;
        reset();
        gn_mode_apply(2, GN_TURING); gn[2].seed = 21; gn[2].turing.len = 7; gn_restart(2);
        project_capture(&q); proj_pack(&st, &q);
        for (i = 0; i + 2u < PROJ_MPC_OCT_WIRE; i++) if (raw[i] == 'G' && raw[i + 1u] == 4u && !memcmp(raw + i + 2u, q.gen, sizeof q.gen)) at = i;
        raw[at + 1u] = 1u;
        for (k = 0; k < PROJ_TRK; k++) memcpy(raw + at + 2u + 14u * k, q.gen[k], 14u);
        memset(raw + at + 2u + 14u * PROJ_TRK, 0, PROJ_GEN_WIRE - 2u - 14u * PROJ_TRK);
        uint32_t h = proj_hash(raw, PROJ_STORE_SIZE - 4u);
        memcpy(raw + PROJ_STORE_SIZE - 4u, &h, 4);
        bad += check("a version 1 block (before CHANGE): loads, CHANGE 0, GATE PAT", at && proj_unpack(&q2, raw) && q2.gen[2][0] == GN_TURING &&
                     q2.gen[2][1] == 21u && q2.gen[2][2] == 7u && !q2.gen[2][14] && !q2.gen[2][16]);
        raw[at + 1u] = 2u;                                  /* version 2: 18 bytes a track, before LOCK */
        for (k = 0; k < PROJ_TRK; k++) { memcpy(raw + at + 2u + 18u * k, q.gen[k], 18u); }
        raw[at + 2u + 18u * 2u + 14u] = 5u;                 /* (track 3's CHANGE) */
        h = proj_hash(raw, PROJ_STORE_SIZE - 4u);
        memcpy(raw + PROJ_STORE_SIZE - 4u, &h, 4);
        bad += check("  a version 2 block (before LOCK): loads, CHANGE kept, LOCK 0", proj_unpack(&q2, raw) && q2.gen[2][0] == GN_TURING &&
                     q2.gen[2][14] == 5u && !q2.gen[2][18]);
        gn_load(2, q2.gen[2]);
        bad += check("  .. SERIES and BOUNCE at their defaults", gn[2].series.intv == 1u && gn[2].bounce.speed == 24u &&
                     gn[2].bounce.balls == 1u && gn[2].mode == GN_TURING);
    }
    reset();
    song.ntrk = NPART;
    gn_mode_apply(2, GN_MARKOV); gn[2].seed = 11; gn_restart(2);
    bad += check("removing track 2: track 3's generator moves to its place", !track_remove(1) && gn[1].mode == GN_MARKOV && gn[1].seed == 11u &&
                 !gn[2].mode && !gn[3].mode);
    template_save();
    gn_mode_apply(0, GN_ATLAS);
    template_load();
    for (k = 0, q.sel = 0; k < GN_TRACKS; k++) q.sel |= gn[k].mode;
    bad += check("a template (a new song): every generator OFF", !q.sel);
    gn_mode_apply(0, GN_ATLAS); gn_mode_apply(3, GN_TURING);
    project_new_blank();
    for (k = 0, q.sel = 0; k < GN_TRACKS; k++) q.sel |= gn[k].mode;
    bad += check("  a blank new song too", !q.sel);
    gn_mode_apply(3, GN_TURING);
    track_blank(3);
    bad += check("  and a track added in a place: no generator", !gn[3].mode);
    return bad;
}

static int page(void)
{
    int bad = 0;
    reset();
    song.sel = 0;
    go_title("GEN");
    bad += check("ARP > GEN on track 1: the page, MODE OFF", cur_page()->scope == SC_GEN && !gn[0].mode);
    turn(EN_K1, 1);
    bad += check("  KNOB 1: TURING", gn[0].mode == GN_TURING);
    song.ntrk = NPART + 1u; song.sel = NPART;
    bad += check("  not on a MIDI-only track", !page_visible(page_titled("GEN")));
    song.sel = 0;
    bad += check("GEN 2 while a generator plays", page_visible(page_titled("GEN 2")));
    gn_mode_apply(0, GN_BOUNCE);
    gn_page_put(6, 4);
    {
        int16_t *v;
        const param_desc_t *d = gn_page_desc(5, &v), *b = gn_page_desc(6, &v);
        bad += check("  BOUNCE: BALLS turned to FALL, ANGLE becomes WIND", gn[0].bounce.balls == GEN_BOUNCE_FALL && str_eq(d->label, "WIND") &&
                     str_eq(b->names[*v], "FALL"));
    }
    gn_mode_apply(0, GN_OFF);
    bad += check("  not with MODE OFF", !page_visible(page_titled("GEN 2")) && page_visible(page_titled("GEN")));
    song.sel = 0;
    return bad;
}

int main(void)
{
    int bad = playback() + changes() + rec_print() + storage() + page();
    printf("%s\n", bad ? "GEN TRACK TEST FAILED" : "generator tracks: all ok");
    return bad != 0;
}
