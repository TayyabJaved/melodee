/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of the M1 / M2 / M3 key layer in hal/fm1_input.h (MELODEE_KEY_NAV): debounced key edges through fm1__key.
 * Checked: off, every key plays as before; F5 / G5 + the six black keys F#4 .. F#5 press FX SCL ENV LFO EDIT GLO /
 * HOME SAVE ARP SEQ PLAY REC, F#5 (M3, alone) + C#5 / D#5 OCT- / OCT+, with one press and one release edge, never a
 * note (C#4 / D#4: notes); F#5 keeps a momentary edit while one is on; a release matches its press
 * whatever the modifiers do in between; M1 + M2 mute new keys; a note held before a modifier keeps sounding;
 * a panel button and its key merge into one press; disarming or remapping with a key down releases what it pressed. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t host_now;
#define FM1_INPUT_NOW() host_now
#define MELODEE_KEY_NAV 1
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_input.h"

enum { M1 = 24, M2 = 26, M3 = 25 };
static const uint8_t MODS[FM1_NAV_ROWS] = {M1, M2, M3};
static const uint8_t TRIG[FM1_NAV_N] = {13, 15, 17, 20, 22, 25};
static const uint8_t MAP[FM1_NAV_ROWS][FM1_NAV_N] = {{2, 3, 4, 5, 6, 7}, {8, 9, 10, 11, 12, 13},
                                                     {0xFF, 0xFF, FM1_NAV_ACTION, 0, 1, 0xFF}};   /* panel.c PANEL_DEFAULT */
static const uint8_t ALT[FM1_NAV_ROWS][FM1_NAV_N] = {{7, 6, 5, 4, 3, 2}, {13, 12, 11, 10, 9, 8},
                                                     {0xFF, 0xFF, FM1_NAV_ACTION, 1, 0, 0xFF}};
static const char *const NAME[FM1_NAV_ROWS][FM1_NAV_N] = {{"FX", "SCL", "ENV", "LFO", "EDIT", "GLO"},
                                                          {"HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"},
                                                          {"", "", "HEAR (no button, the UI's action bit 14)", "OCT-", "OCT+", ""}};

static int fails;
static int check(const char *what, int ok)
{
    printf("%-84s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}

static void contact(uint32_t id, uint32_t closed)
{
    uint32_t i, n = closed ? FM1_DEB_PRESS : FM1_DEB_RELEASE;
    for (i = 0; i < n; i++) {
        host_now += 1000u;
        fm1__key(id, closed);
    }
}
static void down(uint32_t n) { contact(14u + n, 1); }
static void up(uint32_t n) { contact(14u + n, 0); }
static uint32_t pressed, released, noted;
static void take(void)
{
    pressed = fm1_in.pressed;
    released = fm1_in.released;
    noted = fm1_in.notes_pressed;
    fm1_in.pressed = fm1_in.released = fm1_in.notes_pressed = 0;
}
static int quiet(void)
{
    take();
    return !pressed && !released && !noted && !fm1_in.notes && !fm1_in.buttons;
}
static void reset(const uint8_t (*map)[FM1_NAV_N])
{
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    memset((void *)&fm1_nav, 0, sizeof fm1_nav);
    fm1_input_nav(map);
}

int main(void)
{
    char what[128];
    uint32_t n, r, i, ok;

    reset(0);
    for (n = 0, ok = 1; n < 27u; n++) {
        down(n);
        take();
        ok &= fm1_in.notes == 1u << n && noted == 1u << n && !fm1_in.buttons && !pressed;
        up(n);
        take();
        ok &= !fm1_in.notes && !noted && !released;
    }
    check("off: all 27 keys play, F5 / G5 and the triggers too", ok);
    for (i = 0, ok = 1; i < 14u; i++) {
        contact(i, 1);
        take();
        ok &= fm1_in.buttons == 1u << i && pressed == 1u << i;
        contact(i, 0);
        take();
        ok &= !fm1_in.buttons && released == 1u << i;
    }
    check("off: the 14 panel buttons as before", ok);

    reset(MAP);
    for (n = 0, ok = 1; n < 27u; n++) {
        if (n == M1 || n == M2 || n == M3)
            continue;
        down(n);
        take();
        ok &= fm1_in.notes == 1u << n && noted == 1u << n;
        up(n);
        ok &= quiet();
    }
    check("armed, no modifier: every other key plays", ok);

    for (r = 0; r < FM1_NAV_ROWS; r++)
        for (i = 0; i < FM1_NAV_N; i++) {
            uint32_t b = 1u << MAP[r][i];
            if (MAP[r][i] == 0xFF)
                continue;
            reset(MAP);
            down(MODS[r]);
            ok = quiet();
            down(TRIG[i]);
            take();
            ok &= pressed == b && !released && fm1_in.buttons == b && !fm1_in.notes && !noted;
            host_now += 24000000u;                     /* a long press: the level stays */
            ok &= fm1_in.buttons == b;
            up(TRIG[i]);
            take();
            ok &= !pressed && released == b && !fm1_in.buttons && !fm1_in.notes && !noted;
            up(MODS[r]);
            ok &= quiet();
            snprintf(what, sizeof what, "M%u + trigger %u: %s pressed and released once, no note", r + 1u, i + 1u,
                     NAME[r][i]);
            check(what, ok);
        }

    for (r = 0, ok = 1; r < FM1_NAV_ROWS; r++) {
        static const uint8_t OLD[2] = {8, 10};          /* C#4 D#4: OCT- OCT+ with M2 once */
        reset(MAP);
        down(MODS[r]);
        for (i = 0; i < 2u; i++) {
            down(OLD[i]);
            take();
            ok &= fm1_in.notes == 1u << OLD[i] && noted == 1u << OLD[i] && !fm1_in.buttons && !pressed;
            up(OLD[i]);
            take();
            ok &= !fm1_in.notes && !released;
        }
        up(MODS[r]);
        ok &= quiet();
    }
    check("M1 / M2 / M3 + C#4 / D#4: notes (OCT- / OCT+ are M3 + C#5 / D#5)", ok);

    reset(MAP);
    down(M3);
    down(TRIG[3]);
    down(TRIG[4]);
    take();
    ok = pressed == 3u && fm1_in.buttons == 3u && !fm1_in.notes;
    up(M3);
    up(TRIG[3]);
    take();
    ok &= released == 1u && fm1_in.buttons == 2u;
    up(TRIG[4]);
    take();
    ok &= released == 2u && !fm1_in.buttons;
    check("M3 + C#5 + D#5: OCT- and OCT+ held together, each released by its own key", ok);

    reset(MAP);
    down(M1);
    down(M3);
    take();
    ok = pressed == 1u << MAP[0][5] && !fm1_in.notes && fm1_nav.mod == 1u << M1;
    up(M3);
    up(M1);
    take();
    ok &= released == 1u << MAP[0][5] && !fm1_in.buttons;
    check("F#5 with M1 held: GLO, not M3", ok);

    reset(MAP);
    down(M3);
    down(M1);
    take();
    ok = fm1_nav.mod == 1u << M3 && fm1_in.notes == 1u << M1 && noted == 1u << M1 && !fm1_in.buttons;
    down(TRIG[3]);
    take();
    ok &= pressed == 1u << MAP[2][3] && fm1_nav.mod == 1u << M3;
    up(TRIG[3]);
    up(M1);
    take();
    ok &= !fm1_in.notes && !fm1_in.buttons;
    down(M2);
    take();
    ok &= fm1_nav.mod == 1u << M3 && fm1_in.notes == 1u << M2;
    up(M2);
    up(M3);
    take();
    ok &= quiet();
    check("M3 held: F5 and G5 are keys (notes), not M1 / M2; C#5 still OCT-", ok);

    {   /* M3's white keys F3 .. D5: the twelve buttons in one row, M1's, E4 a gap, then M2's (panel.c) */
        static const uint8_t WHITE[FM1_NAV_WHITES] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
        reset(MAP);
        down(M3);
        take();
        ok = quiet();
        for (i = 0; i < FM1_NAV_WHITES; i++) {          /* (no table yet: notes) */
            down(FM1_NAV_WHITE[i]); take();
            ok &= noted == 1u << FM1_NAV_WHITE[i] && !fm1_in.buttons;
            up(FM1_NAV_WHITE[i]); take();
        }
        fm1_input_nav_whites(WHITE);
        for (i = 0; i < FM1_NAV_WHITES; i++) {
            down(FM1_NAV_WHITE[i]); take();
            ok &= pressed == 1u << WHITE[i] && fm1_in.buttons == 1u << WHITE[i] && !noted && !fm1_in.notes;
            up(FM1_NAV_WHITE[i]); take();
            ok &= released == 1u << WHITE[i] && !fm1_in.buttons;
        }
        down(FM1_NAV_WHITE[10]); take();                 /* B4: PLAY, held past M3's release */
        up(M3); take();
        ok &= fm1_in.buttons == 1u << WHITE[10];
        up(FM1_NAV_WHITE[10]); take();
        ok &= released == 1u << WHITE[10] && !fm1_in.buttons && !fm1_in.notes;
        down(11); take();                                /* E4: the gap between GLO and HOME, a note */
        ok &= noted == 1u << 11 && !fm1_in.buttons;
        up(11); take();
        check("M3 + F3 .. D4 FX SCL ENV LFO EDIT GLO, E4 a note, F4 .. D5 HOME SAVE ARP SEQ PLAY REC, held as long as the key",
              ok);

        down(M1); down(FM1_NAV_WHITE[0]); take();
        ok = noted == 1u << FM1_NAV_WHITE[0] && !fm1_in.buttons;
        up(FM1_NAV_WHITE[0]); up(M1); take();
        down(M3); fm1_input_nav_whites_on(0); down(FM1_NAV_WHITE[3]); take();
        ok &= noted == 1u << FM1_NAV_WHITE[3] && !fm1_in.buttons;   /* (ARP > SHAPE: its steps) */
        up(FM1_NAV_WHITE[3]); fm1_input_nav_whites_on(1); down(FM1_NAV_WHITE[3]); take();
        ok &= pressed == 1u << WHITE[3];
        up(FM1_NAV_WHITE[3]); up(M3); take();
        ok &= released == 1u << WHITE[3];
        ok &= quiet();
        check("..only with M3: M1's white keys play; kept notes (ARP > SHAPE) they play too", ok);
    }

    reset(MAP);
    fm1_input_nav_keep(1);
    down(M3);
    take();
    ok = fm1_nav.keep && !fm1_nav.mod && !pressed && !noted && !fm1_in.notes;
    up(M3);
    ok &= quiet();
    fm1_nav.keep = 0;
    down(M1);
    down(M3);
    take();
    ok &= fm1_nav.keep && !pressed && !noted;
    up(M3);
    up(M1);
    ok &= quiet();
    fm1_input_nav_keep(0);
    down(M3);
    up(M3);
    ok &= quiet();
    check("a momentary edit on: F#5 keeps it (alone or with M1 held), no note, no button; off: M3 again", ok);

    for (r = 0, ok = 1; r < FM1_NAV_ROWS; r++) {
        reset(MAP);
        down(MODS[r]);
        ok &= quiet();
        up(MODS[r]);
        ok &= quiet();
    }
    check("a modifier alone (F5, G5, F#5): no note, no button", ok);

    reset(MAP);
    down(M2);
    down(TRIG[5]);
    take();
    up(M2);
    take();
    ok = !released && fm1_in.buttons == 1u << MAP[1][5];
    down(TRIG[0]);
    take();
    ok &= fm1_in.notes == 1u << TRIG[0] && noted == 1u << TRIG[0];
    up(TRIG[0]);
    take();
    up(TRIG[5]);
    take();
    ok &= released == 1u << MAP[1][5] && !fm1_in.buttons && !fm1_in.notes;
    check("modifier let go first: REC held to the trigger's release; the other keys play again", ok);

    reset(MAP);
    down(0);
    down(M1);
    take();
    ok = fm1_in.notes == 1u;
    down(TRIG[1]);
    take();
    ok &= fm1_in.notes == 1u && fm1_in.buttons == 1u << MAP[0][1];
    up(0);
    take();
    ok &= !fm1_in.notes && fm1_in.buttons == 1u << MAP[0][1];
    up(M1);
    up(TRIG[1]);
    take();
    ok &= released == 1u << MAP[0][1] && !fm1_in.buttons && !fm1_in.notes && !noted;
    check("a note held before M1 sounds until its own release", ok);

    reset(MAP);
    down(M1);
    down(2);
    take();
    ok = fm1_in.notes == 1u << 2 && noted == 1u << 2;
    up(2);
    up(M1);
    ok &= quiet();
    check("one modifier + a key that is no trigger: a note", ok);

    reset(MAP);
    down(M1);
    down(M2);
    for (i = 0; i < FM1_NAV_N; i++)
        down(TRIG[i]);
    down(0);
    ok = quiet();
    up(M1);
    ok &= quiet();
    for (i = 0; i < FM1_NAV_N; i++)
        up(TRIG[i]);
    up(0);
    up(M2);
    ok &= quiet();
    check("M1 + M2: new keys do nothing, on press or release", ok);

    reset(MAP);
    down(M1);
    down(TRIG[2]);
    take();
    down(M2);
    up(TRIG[2]);
    take();
    ok = released == 1u << MAP[0][2] && !fm1_in.buttons;
    up(M1);
    up(M2);
    ok &= quiet();
    check("the second modifier while a trigger is down: its button releases as pressed", ok);

    reset(MAP);
    contact(MAP[0][0], 1);
    take();
    ok = pressed == 1u << MAP[0][0];
    down(M1);
    down(TRIG[0]);
    take();
    ok &= !pressed;
    contact(MAP[0][0], 0);
    take();
    ok &= !released && fm1_in.buttons == 1u << MAP[0][0];
    up(TRIG[0]);
    take();
    ok &= released == 1u << MAP[0][0] && !fm1_in.buttons;
    up(M1);
    ok &= quiet();
    check("panel FX and M1 + trigger 1 together: one press, one release", ok);

    reset(MAP);
    down(M1);
    down(TRIG[3]);
    take();
    fm1_input_nav(0);
    up(TRIG[3]);
    take();
    ok = released == 1u << MAP[0][3] && !fm1_in.buttons && !fm1_in.notes;
    up(M1);
    ok &= quiet();
    down(M1);
    take();
    ok &= fm1_in.notes == 1u << M1;
    up(M1);
    take();
    check("disarmed with a trigger down: it releases; then F5 plays", ok);

    reset(MAP);
    down(M2);
    down(TRIG[4]);
    take();
    fm1_input_nav(ALT);
    up(TRIG[4]);
    take();
    ok = released == 1u << MAP[1][4] && !fm1_in.buttons;
    down(TRIG[4]);
    take();
    ok &= pressed == 1u << ALT[1][4];
    up(TRIG[4]);
    up(M2);
    take();
    check("remapped with a trigger down: the old button releases, the new one presses", ok);

    reset(MAP);
    for (i = 0, ok = 1; i < 200u; i++) {
        uint32_t t = TRIG[i % FM1_NAV_N], m = i % FM1_NAV_N < 6u && (i & 2u) ? M1 : M2, o = m == M1 ? M2 : M1;
        down(m);
        down(t);
        up(m);
        if (i & 1u)
            down(o);
        up(t);
        if (i & 1u)
            up(o);
        take();
        ok &= pressed && pressed == released && !fm1_in.buttons && !fm1_in.notes && !noted && !fm1_nav.mod &&
              !fm1_nav.held && !fm1_nav.mute && !fm1_nav.btn;
    }
    check("200 interleaved presses: every press released, no state left", ok);

    if (fails) {
        printf("KEY NAV TEST FAILED (%d)\n", fails);
        return 1;
    }
    printf("key nav: all ok\n");
    return 0;
}
