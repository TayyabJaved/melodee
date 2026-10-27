/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* ARP > MODE SHAPE (firmware/src/arp_shape.c, seq.c arp_step): a chord held on a track in SHAPE plays the next note in
 * DIR's order on each ON step and rests on each OFF one, the order kept over the rests; UP again plays every step. On
 * the ARP page the keys play; M3 (the -nav build) or OCT- held, the white keys are the steps of the page PRESETS picks:
 * a tap toggles one, the ON ones light, the keys no longer play, OCT-'s release no octave. ARP 2 shows STEPS and DIR in
 * PROB and ORD's places without touching those bytes. A song's shapes save in its FBKP record (SHP1) and load back;
 * the dense FBKK fallback has none and loads the defaults. Run by tests/run_tests.sh (also with MELODEE_KEY_NAV). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static const uint8_t CHORD[3] = {64, 60, 67};           /* (held out of order: SHAPE goes low to high) */
static void chord(int on)
{
    if (on) shape.tick[0] = 12345u;                     /* (so the first step, counted 0, shows as a change) */
    for (uint32_t i = 0; i < 3u; i++) {
        if (on) input_on(&trk[0], CHORD[i], 100);
        else input_off(&trk[0], CHORD[i]);
    }
    if (!on) for (uint32_t i = 0; i < 4u; i++) events_block(CTL);
}
/* hold the chord, run the clock for `ticks` steps: each ON step starts order[next++ % n], each OFF step none */
static int plays(uint32_t ticks, const uint8_t *order, uint32_t n)
{
    track_t *t = &trk[0];
    uint32_t seen = 0, next = 0, guard;
    int ok = 1;
    chord(1);
    for (guard = 0; seen < ticks && guard < 200000u; guard++) {
        uint32_t before = shape.tick[0];
        events_block(CTL);
        if (shape.tick[0] == before) continue;
        if (shape_on(0, shape.tick[0])) ok &= t->arp_note == order[next++ % n];
        else ok &= t->arp_note == 0;
        seen++;
    }
    chord(0);
    return ok && seen == ticks;
}
static void edit_mod(int on)                            /* M3 in the -nav build, else OCT- */
{
#if MELODEE_KEY_NAV
    if (on) fm1_nav.mod |= 1u << FM1_NAV_MOD[2]; else fm1_nav.mod &= ~(1u << FM1_NAV_MOD[2]);
#else
    if (on) fm1_in.buttons |= 1u << panel.btn[B_OCTDN]; else fm1_in.buttons &= ~(1u << panel.btn[B_OCTDN]);
#endif
    frame();
}

int main(void)
{
    static const uint8_t UP[3] = {60, 64, 67}, DN[3] = {67, 64, 60};
    int bad = 0, ok;
    int16_t *vp;
    track_t *t;
    ui_power_on();
    for (uint32_t k = 0; k < NTRK; k++) shape_default(k);
    t = &trk[0];
    track_select(0);
    t->p[P_AMODE] = AM_SHAPE; t->p[P_AOCT] = 1; t->p[P_AGATE] = 64; t->p[P_AHOLD] = 0; t->p[P_APROB] = 0;
    events_block(CTL);
    bad += check("SHAPE's default: every step ON, 16 of them: the UP arp (PROB 0 ignored)", plays(12, UP, 3));
    shape.ctrl[0][1] = 8;
    shape.mask[0] = 0x6Du;                              /* x.xx.xx. */
    bad += check("x.xx.xx. over 8 steps: ON steps play C E G on, OFF steps rest, 24 steps", plays(24, UP, 3));
    shape.ctrl[0][0] = SD_DOWN;
    bad += check("DIR DOWN: G E C on the ON steps", plays(16, DN, 3));
    shape.ctrl[0][0] = SD_UP;
    t->p[P_AMODE] = 1;                                  /* UP: the old arp, every step */
    t->p[P_APROB] = 127;
    ok = 1;
    {
        uint32_t played = 0, before = t->arp_idx;
        chord(1);
        for (uint32_t i = 0; i < 4000u; i++) { events_block(CTL); played += t->arp_idx != before; before = t->arp_idx; }
        chord(0);
        ok = played > 8u;
    }
    bad += check("UP again: the old arp", ok);

    /* the keys: they play; the edit modifier held, the white keys are the steps (page 1, then 2 with PRESETS) */
    t->p[P_AMODE] = AM_SHAPE;
    shape.ctrl[0][1] = 32; shape.mask[0] = 0;
    go_title("ARP"); frame();
    uint32_t w0 = 1u << key_at(0, 0), w1 = 1u << key_at(0, 1), b0 = 1u << key_at(1, 0), whites, lit;
    tap_key(key_at(0, 1));
    ok = keys_mode() != 3u && shape.mask[0] == 0;
    edit_mod(1);
    ok &= keys_mode() == 3u && song.grid == 3u;
    tap_key(key_at(0, 1));
    ok &= shape.mask[0] == 2u;
    lit = shape_leds(&whites);
    ok &= lit == w1 && (whites & w0) && !(whites & b0) && __builtin_popcount(whites) == 16;
    turn(EN_PRESET, 1);
    ok &= shape_page() == 1u;
    tap_key(key_at(0, 0));
    ok &= shape.mask[0] == (2u | (1ull << 16));
    {   /* the audio side: a white key is the UI's, a black key still plays */
        uint32_t prev = fm1_in.notes;
        fm1_in.notes = w0 | b0; keyboard_block();
        ok &= kb_note[key_at(0, 0)] == KB_SILENT && kb_note[key_at(1, 0)] != KB_SILENT;
        fm1_in.notes = prev; keyboard_block();
    }
    int32_t octave = song.octave;
    edit_mod(0);
    ok &= keys_mode() != 3u && song.octave == octave;
    bad += check("keys play; M3 / OCT- held: white keys toggle the page's steps, lit where ON, silent, no octave", ok);

    /* ARP 2: STEPS and DIR in PROB and ORD's places */
    int16_t prob = t->p[P_APROB], ord = t->p[P_AORDER];
    go_title("ARP 2");
    const param_desc_t *d = page_desc(cur_page(), 1, &vp);
    ok = d && str_eq(d->label, "STEPS") && vp == &shape.ctrl[0][1];
    turn(EN_K2, 3);
    ok &= shape.ctrl[0][1] == 35;
    d = page_desc(cur_page(), 3, &vp);
    ok &= d && str_eq(d->label, "DIR") && vp == &shape.ctrl[0][0];
    ok &= t->p[P_APROB] == prob && t->p[P_AORDER] == ord;
    go_title("ARP");
    d = page_desc(cur_page(), 2, &vp);
    ok &= d && str_eq(d->label, "OCT");
    bad += check("ARP 2 in SHAPE: STEPS and DIR, the PROB ORD bytes kept; ARP keeps OCT", ok);

    /* persistence: this song's shape goes in its FBKP record (SHP1) and comes back */
    uint64_t mask = shape.mask[0];
    ok = !project_save(0);
    uint32_t magic, shp;
    memcpy(&magic, proj_bank_slot[0], 4); memcpy(&shp, proj_bank_slot[0] + BANK_SHAPE_OFF, 4);
    ok &= magic == BANKP_MAGIC && shp == BANK_SHAPE_MAGIC;
    shape_default(0);
    project_load(0);
    ok &= shape.mask[0] == mask && shape.ctrl[0][1] == 35 && shape.ctrl[0][0] == SD_UP;
    bad += check("a song's own shape: FBKP with SHP1, saved and loaded back exactly", ok);

    ui_power_on();                                      /* the dense FBKK fallback (FBKP did not fit): no shapes */
    for (uint32_t k = 0; k < NTRK; k++) shape_default(k);
    shape.ctrl[0][1] = 9;
    bank_force_v1 = 1;
    ok = !project_save(1);
    bank_force_v1 = 0;
    memcpy(&magic, proj_bank_slot[1], 4);
    ok &= magic == BANK_MAGIC;
    project_load(1);
    ok &= shape.ctrl[0][1] == SHAPE_DEF_STEPS && shape.mask[0] == ~(uint64_t)0;
    bad += check("the FBKK fallback has no SHP1: it loads the default shapes", ok);

    printf(bad ? "SHAPE arp: %d FAILED\n" : "SHAPE arp: rhythm, keys, pages, persistence: OK\n", bad);
    return bad != 0;
}
