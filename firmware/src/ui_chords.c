/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SEQ > CHORDS, CHORDS 2 (params.c PAGES; a melodic track's): the chord sequencer. A step holds one note, its root,
 * and what to play on it (chord_seq.c): CHORDS turns NOTE CHORD VOICE STRUM, CHORDS 2 DIR VEL LENGTH NUDGE; PRESETS
 * walks the steps, empty ones too, and the header names the step. NOTES' roll under the knobs shows the notes each
 * step plays (an entry's strings where its strum and nudge put them).
 * A key or a MIDI note sets the step's note and keeps its chord; an empty step takes the chord last set or passed
 * over, which the keys play while the page shows (chord_seq.c cseq_key); letting go moves on, as on NOTES. NOTE or
 * CHORD turned on an empty step adds an entry on the last note. EDIT deletes it; OCT+ opens the note's sheet; undo
 * and moves are NOTES'. A step of several notes (a NOTES chord) or recorded notes plays as it is; CHORD turns the
 * former into an entry on its lowest note. Included at the end of ui_input.c */
#define CSEQ_ID 4u                                      /* (CHORDS: ids 4..7, CHORDS 2: 8..11; NOTES: 0..3) */
static int cseq_page_i(uint32_t i)
{
    return PAGES[i].scope == SC_STEP && PAGES[i].graph == GR_ROLL && PAGES[i].id[0] >= CSEQ_ID;
}
static uint32_t cseq_page(void)                        /* 1 CHORDS, 2 CHORDS 2, 0 neither */
{
    if (ui.home || drum_track(TSEL) || !cseq_page_i(ui.page))
        return 0;
    return PAGES[ui.page].id[0] >= 2u * CSEQ_ID ? 2u : 1u;
}

static struct { uint8_t set, cursor, trk; } cseq_ui __attribute__((section(".pool")));
static cseq_t *cseq_pen(void)                          /* the chord a new entry gets (DIA3 at first) */
{
    if (!cseq_ui.set) {
        cseq_rest(&cseq.pen);
        cseq.pen.v[CS_CHORD] = 1;
        cseq_ui.set = 1;
    }
    return &cseq.pen;
}
/* step st becomes an entry: note and settings c */
static void cseq_write(step_t *st, uint32_t note, const cseq_t *c)
{
    st->note[0] = (uint8_t)note;
    st->n = 1;
    st->time = ST_NOTE;
    st->hit = st->acc = 0;
    st->flags &= (uint8_t)~SF_RECORDED;
    cseq_put(st, c);
}

/* the notes step st plays -> out (an entry: its chord; else its own notes), at[j] each one's onset after the step's
 * (an entry's nudge and strum, in the clock's units; period: a step) -> how many */
static uint32_t cseq_roll(const track_t *t, const step_t *st, uint8_t *out, int32_t *at, uint32_t period)
{
    cseq_t c;
    uint32_t n, i;
    if (!cseq_get(st, &c)) {
        for (i = 0; i < st->n && i < CHORD_MAX; i++) {
            out[i] = st->note[i];
            at[i] = 0;
        }
        return i;
    }
    n = cseq_chord(t, st->note[0], &c, out);
    for (i = 0; i < n; i++) {
        chord_strum_t s = {0, seq_beat_samples(), (uint8_t)n, c.v[CS_STRUM], 0};
        uint32_t order = c.v[CS_DIR] == 1u ? n - 1u - i : i;   /* (UP: the top first; ALT drawn as DOWN, its first) */
        at[i] = (int32_t)period * cseq_nudge(&c) / 24 +
                (c.v[CS_STRUM] && n > 1u && !t->p[P_AMODE] ? (int32_t)chord_strum_due(&s, order, 0) : 0);
    }
    return n;
}

/* the knobs: CHORDS 0 NOTE 1 CHORD 2 VOICE 3 STRUM, CHORDS 2 4 DIR 5 VEL 6 LENGTH 7 NUDGE; the settings' (cseq_t) */
static const char *const CS_LABEL[8] = {"NOTE", "CHRD", "VOICE", "STRUM", "DIR", "VEL", "LENGTH", "NUDGE"};   /* (CHRD: as SCL > CHORD; CHORD is cut in Rubik) */
static const char *const *const CS_NAME[CS_NUDGE] = {N_CHRD, N_VOIC, N_STRUM, N_STROKE};
static const uint8_t CS_FIELD[8] = {CS_N, CS_CHORD, CS_VOICE, CS_STRUM, CS_DIR, CS_N, CS_N, CS_NUDGE};

/* the step the cursor's note starts on (NSTEP: none) and its settings (a plain note: at rest) -> 1: one note */
static int cseq_sel(uint32_t *at, cseq_t *c)
{
    *at = notes_manual_start(TSEL);
    if (*at < NSTEP && !cseq_get(&TSEL->step[*at], c))
        cseq_rest(c);
    return *at < NSTEP && TSEL->step[*at].n == 1u;
}

/* the cards. "--": nothing there; a setting that does nothing now (VOICE without a chord, DIR without a strum, no
 * nudge) dim */
static int cseq_columns(void)
{
    uint32_t p = cseq_page(), at, k, f, i;
    int one;
    const step_t *st;
    cseq_t c;
    char v[12], u[8];
    if (!p)
        return 0;
    one = cseq_sel(&at, &c);
    st = at < NSTEP ? &TSEL->step[at] : 0;
    for (k = 0; k < 4u; k++) {
        int on = st != 0;
        f = (p - 1u) * 4u + k;
        i = CS_FIELD[f];
        str_cpy(v, "--", sizeof v);
        u[0] = 0;
        if (f == 0u && st) {
            note_name(v, st->note[0]);
            if (st->n > 1u) {
                u[0] = '+';
                fmt_int(u + 1, (int32_t)st->n - 1);
            }
        } else if (f == 5u && st) {
            fmt_int(v, st->flags & SF_ACCENT ? 127 : st->vel ? st->vel : 96);
        } else if (f == 6u && st) {
            fmt_int(v, (int32_t)step_note_length(TSEL, at));
        } else if (i == CS_NUDGE) {
            int32_t pc = (cseq_nudge(&c) * 100 + (cseq_nudge(&c) < 0 ? -12 : 12)) / 24;   /* (24ths as a percentage) */
            on = one && pc;
            if (one) {
                v[0] = '+';
                fmt_int(v + (pc > 0), pc);
                str_cpy(u, "%", sizeof u);
            }
        } else if (i < CS_N) {
            on = one && c.v[CS_CHORD] && (i != CS_DIR || c.v[CS_STRUM]);
            if (one)
                str_cpy(v, CS_NAME[i][c.v[i]], sizeof v);
        }
        draw_column(k, CS_LABEL[f], v, u, on ? VAL(k) : T_DIM, -1, f == 0u ? ICON_PITCH : ICON_AUTO);
    }
    return 1;
}

/* the header: the page and the step ("CHORDS 3/16") */
static void cseq_chip(char *b)
{
    str_cpy(b, "CHORDS ", 16);
    fmt_int(b + str_len(b), (int32_t)ui.cursor + 1);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), TSEL->p[P_SLEN]);
}

/* a knob turned (CS_LABEL's) */
static int cseq_edit(uint32_t slot, int32_t steps)
{
    uint32_t p = cseq_page(), f, at, i;
    step_t *st;
    cseq_t c;
    if (!p)
        return 0;
    f = (p - 1u) * 4u + slot;
    i = CS_FIELD[f];
    if (live_rec_sel()) {
        ui_message("STOP RECORDING");
        return 1;
    }
    fm1_irq_off();
    if (!cseq_sel(&at, &c) && f != 5u && f != 6u) {
        st = &TSEL->step[at < NSTEP ? at : ui.cursor];
        if (at < NSTEP && f == 1u) {                    /* a NOTES chord: an entry on its lowest note */
            uint32_t low = st->note[0];
            for (at = 1; at < st->n; at++)
                if (st->note[at] < low)
                    low = st->note[at];
            c = *cseq_pen();
            c.v[CS_CHORD] = c.v[CS_CHORD] ? c.v[CS_CHORD] : 1u;
            cseq_write(st, low, &c);
        } else if (at >= NSTEP && f < 2u && !(st->flags & SF_RECORDED) && st->time != ST_TIE) {
            c = *cseq_pen();                            /* an empty step: NOTE, CHORD add one (that turn) */
            cseq_write(st, last_note, &c);
        } else {
            fm1_irq_on();
            ui_message(at < NSTEP ? "TURN CHORD FIRST" : "SELECT A NOTE");
            return 1;
        }
        *cseq_pen() = c;
        fm1_irq_on();
        notes_preview();
        return 1;
    }
    if (at >= NSTEP) {
        fm1_irq_on();
        ui_message("SELECT A NOTE");
        return 1;
    }
    st = &TSEL->step[at];
    if (f == 0u) {
        st->note[0] = (uint8_t)clamp((int32_t)st->note[0] + steps, 1, 127);
        last_note = st->note[0];
    } else if (f == 5u) {
        st->vel = (uint8_t)clamp((int32_t)(st->flags & SF_ACCENT ? 127 : st->vel ? st->vel : 96) + steps, 1, 127);
        st->flags &= (uint8_t)~SF_ACCENT;
    } else if (f == 6u) {
        step_note_resize(TSEL, at, (int32_t)step_note_length(TSEL, at) + steps);
    } else {
        c.v[i] = (uint8_t)clamp((int32_t)c.v[i] + steps, 0, CS_RANGE[i] - 1);
        cseq_put(st, &c);
        *cseq_pen() = c;
    }
    fm1_irq_on();
    if (f < 3u)
        notes_preview();
    return 1;
}

static int cseq_preset(int32_t s)                      /* PRESETS: the step */
{
    if (!cseq_page())
        return 0;
    cursor_set((int32_t)ui.cursor + s);
    return 1;
}

/* a key's note (or a MIDI note's) entered: the cursor step's root, its chord kept (else the pen's). The first of
 * the keys pressed together; seq_entry_finish moves on when they are let go */
static int cseq_entry_root(uint32_t note)
{
    step_t *st;
    cseq_t c;
    if (!cseq_page())
        return 0;
    if (ui.entry_open)
        return 1;
    st = &TSEL->step[ui.cursor];
    step_history.cursor_before = ui.cursor;
    ui.entry_open = 1;
    fm1_irq_off();
    if (!cseq_get(st, &c))
        c = *cseq_pen();
    cseq_write(st, note, &c);
    fm1_irq_on();
    return 1;
}

/* notes_preview on an entry: its chord, together -> how many (nn, vv), 0: not one */
static uint32_t cseq_heard(const step_t *st, uint8_t *nn, uint8_t *vv)
{
    uint32_t n, i;
    int32_t at[CHORD_MAX];
    cseq_t c;
    if (!cseq_page() || !cseq_get(st, &c))
        return 0;
    n = cseq_roll(TSEL, st, nn, at, 0);
    for (i = 0; i < n; i++)
        vv[i] = st->vel ? st->vel : 96;
    return n;
}

/* each pass: the keys play the pen's chord while the page shows (not while recording); arriving on an entry, its
 * chord is the pen's */
static void cseq_frame(void)
{
    uint32_t at;
    cseq_t c;
    if (!cseq_page() || live_rec_sel()) {
        cseq.live = 0;
        return;
    }
    if ((ui.cursor != cseq_ui.cursor || song.sel != cseq_ui.trk) && cseq_sel(&at, &c) &&
        cseq_get(&TSEL->step[at], &c))
        *cseq_pen() = c;
    cseq_ui.cursor = ui.cursor;
    cseq_ui.trk = song.sel;
    (void)cseq_pen();
    cseq.live = (uint8_t)(song.sel + 1u);
}
