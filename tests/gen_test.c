/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of the generators copied from Tessera (firmware/src/gen.c), alone: TURING, EUCLID, MARKOV and ATLAS (Grids).
 * A seed plays the same sequence every time; PROB 0 locks TURING's loop to its LEN; EUCLID spreads exactly PULSE hits
 * over STEPS, any rotation; MARKOV's DENS 100 plays every step, its notes within SPAN; ATLAS reads the Grids map
 * unchanged (a node's own chances at its corner) and DENS 0 plays nothing, DENS 100 every chance above 0; SERIES is
 * Nørgård's infinity series (0 1 -1 2 1 0 -2 3 ..), LOOP repeats it; BOUNCE's balls stay in the box, a wall hit plays a
 * note in range; FALL's leaves land and play, three notes on a step at most. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/gen.h"
#include "../firmware/src/gen.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%-84s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) fails++;
    return ok;
}
static const gen_clock_t CLK = {0, 16, 0};

static uint32_t turing_run(uint32_t seed, uint32_t prob, uint8_t *notes, uint32_t n)
{
    gen_turing_t g = {gen_turing_pick(seed), 8, (uint8_t)prob, 0, 0, 2};
    uint32_t rnd = gen_stream(seed), hits = 0;
    for (uint32_t i = 0; i < n; i++) {
        gen_out_t o = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
        gen_turing_tick(&g, &CLK, &rnd, &o);
        notes[i] = o.gate ? o.note : 0;
        hits += o.gate;
        if (o.gate && (o.note < 36u || o.note >= 36u + 2u * 12u + 12u)) return 0xFFFFu;
    }
    return hits;
}

int main(void)
{
    uint8_t a[64], b[64];
    uint32_t i, ok;

    ok = turing_run(42, 30, a, 64) != 0xFFFFu && turing_run(42, 30, b, 64) != 0xFFFFu && !memcmp(a, b, sizeof a);
    turing_run(43, 30, b, 64);
    check("TURING: a seed plays the same 64 steps again, another seed others, notes within SPAN", ok && memcmp(a, b, sizeof a));
    turing_run(7, 0, a, 64);
    for (i = 8, ok = 1; i < 64u; i++) ok &= a[i] == a[i - 8u];
    check("TURING: PROB 0 locks the loop to its LEN (8 steps)", ok);

    for (uint32_t steps = 1, all = 1; steps <= 32u; steps++)
        for (uint32_t pulses = 0; pulses <= steps; pulses++)
            for (uint32_t rot = 0; rot < steps; rot++) {
                uint32_t n = 0;
                for (i = 0; i < steps; i++) n += (uint32_t)gen_euclid_hit((uint8_t)i, (uint8_t)steps, (uint8_t)pulses, (uint8_t)rot);
                all &= n == pulses;
                if (steps == 32u && pulses == 32u && rot == 31u)
                    check("EUCLID: exactly PULSE hits over STEPS for every STEPS, PULSE and ROT", all);
            }
    {
        gen_euclid_t g = {16, 5, 0, 0, 0, 9};
        uint32_t rnd = 1, n = 0, same = 1;
        uint8_t first[16];
        for (i = 0; i < 32u; i++) {
            gen_out_t o = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
            gen_euclid_tick(&g, &CLK, &rnd, &o);
            n += o.gate;
            if (i < 16u) first[i] = o.gate ? o.note : 0;
            else same &= first[i - 16u] == (o.gate ? o.note : 0);
        }
        check("EUCLID: 5 of 16 plays 10 hits in two cycles, the same notes each cycle", n == 10u && same);
    }

    {
        gen_markov_t g = {0, 0, 50, 100, 2};
        uint32_t rnd = gen_stream(5), gates = 0, inside = 1;
        gen_clock_t clk = {0, 16, 3};
        for (i = 0; i < 200u; i++) {
            gen_out_t o = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
            gen_markov_tick(&g, &clk, &rnd, &o);
            gates += o.gate;
            inside &= o.note >= 36u + 3u && o.note <= 36u + 3u + 14u + 12u && g.cur_state < 8u;
        }
        check("MARKOV: DENS 100 plays every step, notes within the key's two octaves", gates == 200u && inside);
    }

    {
        gen_grids_t g;
        uint32_t p, s, same = 1, none = 1, all = 1;
        memset(&g, 0, sizeof g);
        for (p = 0; p < 3u; p++)
            for (s = 0; s < 32u; s++) {
                uint32_t v = GEN_GRIDS_NODE[0][p * 32u + s];   /* (Grids' U8Mix at its corner: x 255 / 256, twice) */
                same &= gen_grids_level(s, p, 0, 0) == ((v * 255u >> 8) * 255u >> 8);
            }
        check("ATLAS: the Grids map read unchanged (node at X 0, Y 0, through Grids' blend)", same);
        for (p = 0; p < 3u; p++)
            for (s = 0; s < 32u; s++) {
                int acc;
                g.dens = 0;
                none &= !gen_grids_hit(&g, s, 32, p, 0, &acc);
                g.dens = 100;
                all &= gen_grids_hit(&g, s, 32, p, 0, &acc) == (gen_grids_level(s, p, 0, 0) > 0u);
            }
        check("ATLAS: DENS 0 plays nothing, DENS 100 every step the map gives a chance", none && all);
    }

    {
        static const int8_t NORGARD[16] = {0, 1, -1, 2, 1, 0, -2, 3, -1, 2, 0, 1, 2, -1, -3, 4};
        gen_series_t g = {0, 1, 4, 0, 0};
        uint8_t n[12];
        for (i = 0, ok = 1; i < 16u; i++) ok &= gen_sr_value(i) == NORGARD[i];
        check("SERIES: Nørgård's infinity series, its first 16 numbers", ok);
        for (i = 0, ok = 1; i < 12u; i++) {
            gen_out_t o = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
            gen_series_tick(&g, &o);
            n[i] = o.note;
            ok &= o.gate && o.note >= 48u && o.note <= 48u + 24u + 12u;
        }
        check("  a note every step, LOOP 4 repeats every 4", ok && !memcmp(n, n + 4, 8) && memcmp(n, n + 1, 3));
    }
    {
        gen_bounce_t g;
        uint32_t rnd = gen_stream(9), notes = 0, inside = 1, chords = 0;
        memset(&g, 0, sizeof g);
        g.speed = 24; g.angle = 37; g.balls = 3;
        gen_bounce_serve(&g, 9);
        for (i = 0; i < 600u; i++) {
            gen_out_t o = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
            gen_bounce_tick(&g, &CLK, &rnd, &o);
            notes += o.gate; chords += o.more_n > 0u;
            inside &= o.more_n <= 2u && (!o.gate || (o.note >= 48u && o.note <= 48u + 12u + 14u));
        }
        check("BOUNCE 3 balls: they hit walls and play, now and then together, notes in range", notes > 20u && chords > 0u && inside);
        g.balls = GEN_BOUNCE_FALL; g.angle = 70;
        gen_bounce_serve(&g, 9);
        for (i = 0, notes = 0, inside = 1; i < 600u; i++) {
            gen_out_t o = {0, 60, 100, 0, 0, 0, {0, 0}, 0, 0};
            gen_bounce_tick(&g, &CLK, &rnd, &o);
            notes += o.gate;
            inside &= o.more_n <= 2u;
            for (uint32_t k = 0; k < GEN_FALL_LEAVES; k++) inside &= g.lx[k] <= 255u;
        }
        check("FALL (BALLS 5): leaves land and play, three notes on a step at most", notes > 20u && inside);
    }

    if (fails) {
        printf("GEN TEST FAILED (%d)\n", fails);
        return 1;
    }
    printf("generators: all ok\n");
    return 0;
}
