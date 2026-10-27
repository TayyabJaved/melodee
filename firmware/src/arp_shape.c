/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* ARP > MODE SHAPE: a rhythm for the arp. Each of a track's STEPS steps (1..64) is ON or OFF. An ON step plays the next
 * held note in DIR's order (UP DN UPDN RND, as the arp modes of those names), an OFF step rests and leaves the order
 * where it is. OCT RATE GATE SWG HOLD are the arp's; PROB and ORD play no part (the notes go low to high). seq.c
 * arp_step plays it; the ARP page edits it (ui.c / ui_input.c shape_*: M3 or OCT- held, the white keys are the steps of
 * the page PRESETS picks); project.c and pattern_store.c store it, SHAPE_BYTES a track. The audio interrupt reads it:
 * the UI changes a mask with the interrupts off. Included by params.c */
#ifndef ARP_SHAPE_C
#define ARP_SHAPE_C
#define AM_SHAPE 7u                      /* P_AMODE: after REPEAT */
#define SHAPE_STEPS 64u
#define SHAPE_DEF_STEPS 16
#define SHAPE_BYTES 10u                  /* stored: the mask (8, little-endian), STEPS, DIR */
enum { SD_UP, SD_DOWN, SD_UPDN, SD_RAND, SD_N };
static struct {
    uint64_t mask[NTRK];                 /* step s ON: bit s */
    int16_t ctrl[NTRK][2];               /* DIR, STEPS (the ARP 2 knobs); STEPS 0: never set (the pool's zeros) */
    uint32_t tick[NTRK];                 /* the step played last, counted since the chord began */
} shape __attribute__((section(".pool")));

static void shape_default(uint32_t k)    /* every step ON, 16 of them, UP: SHAPE starts as the UP arp */
{
    k %= NTRK;
    shape.mask[k] = ~(uint64_t)0;
    shape.ctrl[k][0] = SD_UP;
    shape.ctrl[k][1] = SHAPE_DEF_STEPS;
}
static uint32_t shape_steps(uint32_t k)  /* (a track never set, or set out of range: the default first) */
{
    k %= NTRK;
    if (shape.ctrl[k][1] < 1 || shape.ctrl[k][1] > (int16_t)SHAPE_STEPS || shape.ctrl[k][0] < 0 || shape.ctrl[k][0] >= SD_N)
        shape_default(k);
    return (uint32_t)shape.ctrl[k][1];
}
static int shape_on(uint32_t k, uint32_t step) { return (int)((shape.mask[k % NTRK] >> (step % shape_steps(k))) & 1u); }
static uint32_t shape_mode(uint32_t k) { (void)shape_steps(k); return (uint32_t)shape.ctrl[k % NTRK][0] + 1u; }   /* (UP..RND) */

/* the stored form */
static void shape_raw_default(uint8_t *b)
{
    for (uint32_t i = 0; i < 8u; i++) b[i] = 0xFFu;
    b[8] = SHAPE_DEF_STEPS;
    b[9] = SD_UP;
}
static int shape_raw_valid(const uint8_t *b) { return b[8] >= 1u && b[8] <= SHAPE_STEPS && b[9] < SD_N; }
static void shape_pack(uint32_t k, uint8_t *b)
{
    uint32_t steps = shape_steps(k);
    for (uint32_t i = 0; i < 8u; i++) b[i] = (uint8_t)(shape.mask[k % NTRK] >> (8u * i));
    b[8] = (uint8_t)steps;
    b[9] = (uint8_t)shape.ctrl[k % NTRK][0];
}
static void shape_unpack(uint32_t k, const uint8_t *b)   /* (a bad one: the default) */
{
    uint64_t m = 0;
    k %= NTRK;
    if (!shape_raw_valid(b)) { shape_default(k); return; }
    for (uint32_t i = 0; i < 8u; i++) m |= (uint64_t)b[i] << (8u * i);
    shape.mask[k] = m;
    shape.ctrl[k][0] = b[9];
    shape.ctrl[k][1] = b[8];
}
#endif
