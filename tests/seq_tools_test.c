/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Pattern sheet's Double, Half, Euclid and Euclid turn (firmware/src/seq_tools.c) against the real steps, recorded
 * notes, motion and undo: Euclid spreads its hits over LEN from step 1 into one note row (ROOT on an empty pattern) or
 * the drum grid's lane, the other notes and lanes staying; its turn moves them; a pattern with recorded notes is
 * refused; Double copies LEN steps after themselves, Half halves LEN with the rest kept hidden; 64 steps and 1 step are
 * their limits; a running song refuses them; EDIT held undoes each; the sheet holds them. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void reset(void)
{
    ui_power_on();
    set_engine_of(&trk[0], HOST_SYNTH);
    trk[0].p[P_ROOT] = 0; trk[0].p[P_SLEN] = 16;
    for (uint32_t i = 0; i < NSTEP; i++) step_clear(&trk[0].step[i]);
    pattern_commit(&trk[0]);
    song.sel = 0;
    seqt.hits = 4; seqt.rot = 0;
}
static int row_has(const track_t *t, uint32_t i, uint32_t x)
{
    for (uint32_t j = 0; j < t->step[i].n; j++) if (t->step[i].time == ST_NOTE && t->step[i].note[j] == x) return 1;
    return 0;
}
static uint32_t row_mask(const track_t *t, uint32_t x)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < 16u; i++) m |= (uint32_t)row_has(t, i, x) << i;
    return m;
}

static int euclid(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    step_t before[NSTEP];
    reset();
    seqt.hits = 5;
    seqt_euclid();
    bad += check("Euclid 5 of 16 on an empty pattern: C4 (ROOT) on steps 1 5 8 11 14", row_mask(t, 60) == 0x2491u && msg_is("EUCLID"));
    seqt_rot_turn(2);
    seqt_euclid();
    bad += check("  Euclid turn 2: the same hits two steps later, the old ones gone", row_mask(t, 60) == 0x9244u);
    t->step[3].time = ST_NOTE; t->step[3].n = 1; t->step[3].note[0] = 64;   /* (E4 and G4 after the first C4: Euclid's */
    t->step[5].time = ST_NOTE; t->step[5].n = 1; t->step[5].note[0] = 67;   /* row stays C4) */
    memcpy(before, t->step, sizeof before);
    seqt.hits = 8; seqt.rot = 0;
    seqt_euclid();
    bad += check("  8 of 16: every other step; E4 and G4 on the steps between stay", row_mask(t, 60) == 0x5555u && row_has(t, 3, 64) &&
                 row_has(t, 5, 67));
    hold(B_EDIT);
    bad += check("  EDIT held: the pattern as before", !memcmp(t->step, before, sizeof before));

    reset();
    set_engine_of(t, ENGI_DRUM);
    ui.lane = 2;
    t->step[5].time = ST_NOTE; t->step[5].hit = 1u;      /* (a kick: lane 0) */
    seqt.hits = 3;
    seqt_euclid();
    {
        uint32_t m = 0;
        for (uint32_t i = 0; i < 16u; i++) m |= (uint32_t)((t->step[i].hit >> 2) & 1u) << i;
        bad += check("Euclid on the drum grid: 3 of 16 on its lane, the kick on another lane stays", m == 0x0841u && (t->step[5].hit & 1u));
    }

    reset();
    {
        recorded_note_t r;
        memset(&r, 0, sizeof r);
        recorded_set_note(&r, 60); r.vel = 90; r.duration = 128;
        recorded_set_owner(&r, recording_owner(t)); recorded_set_step(&r, 0, 0, 0);
        recording[0] = r; recording_reindex();
        t->step[0].time = ST_NOTE; t->step[0].flags |= SF_RECORDED;
    }
    seqt_euclid();
    bad += check("  a pattern with recorded notes: refused, nothing written", msg_is("RECORDED NOTES HERE") && row_mask(t, 60) == 0u);
    memset(recording, 0, sizeof recording); recording_reindex();
    return bad;
}

static int lengths(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, ok;
    reset();
    for (i = 0; i < 16u; i++) { t->step[i].time = ST_NOTE; t->step[i].n = 1; t->step[i].note[0] = (uint8_t)(48u + i); }
    t->step[20].time = ST_NOTE; t->step[20].n = 1; t->step[20].note[0] = 99;   /* (hidden, past LEN) */
    pattern_commit(t);
    seqt_double();
    for (i = 0, ok = t->p[P_SLEN] == 32; i < 16u; i++) ok &= t->step[16u + i].note[0] == 48u + i && t->step[i].note[0] == 48u + i;
    bad += check("Double: LEN 32, the 16 steps again after themselves", ok && msg_is("PATTERN DOUBLED"));
    hold(B_EDIT);
    bad += check("  EDIT held: LEN 16 again", t->p[P_SLEN] == 16);
    seqt_half();
    bad += check("Half: LEN 8", t->p[P_SLEN] == 8 && msg_is("PATTERN HALVED"));
    bad += check("  the steps past it kept, hidden", t->step[12].note[0] == 60u && t->step[15].note[0] == 63u);
    hold(B_EDIT);
    bad += check("  EDIT held: LEN 16 again", t->p[P_SLEN] == 16);
    t->p[P_SLEN] = 64; pattern_commit(t);
    seqt_double();
    bad += check("64 steps: Double refused", msg_is("64 STEPS MAX") && t->p[P_SLEN] == 64);
    t->p[P_SLEN] = 1; pattern_commit(t);
    seqt_half();
    bad += check("1 step: Half refused", msg_is("1 STEP MIN") && t->p[P_SLEN] == 1);
    t->p[P_SLEN] = 16; pattern_commit(t);
    chain.running = 1;
    seqt_double();
    ok = msg_is("STOP TO EDIT") && t->p[P_SLEN] == 16;
    seqt_euclid();
    ok &= msg_is("STOP TO EDIT");
    chain.running = 0;
    bad += check("a song playing: Double and Euclid refused", ok);
    return bad;
}

static int sheet(void)
{
    int bad = 0;
    uint32_t i, rows = 0;
    reset();
    go_title("PATTERN");
    page_sheet_open();
    for (i = 0; i < pop.n; i++)
        rows += str_eq(pop.rows[i].label, "Double") || str_eq(pop.rows[i].label, "Half") || str_eq(pop.rows[i].label, "Euclid") ||
                str_eq(pop.rows[i].label, "Euclid turn");
    bad += check("PATTERN's sheet (OCT+): Double, Half, Euclid, Euclid turn", pop.on == POP_SHEET && rows == 4u);
    {
        char b[24];
        seqt.hits = 5; seqt_hits_v(b);
        bad += check("  Euclid's value: \"5 of 16 C4\"", str_eq(b, "5 of 16 C4"));
    }
    pop_close();
    return bad;
}

int main(void)
{
    int bad = euclid() + lengths() + sheet();
    printf("%s\n", bad ? "SEQ TOOLS TEST FAILED" : "pattern tools: all ok");
    return bad != 0;
}
