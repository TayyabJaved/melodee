/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Song sections' sheets (ours): SONG's (OCT+ held: a column's section placed again or as a new letter, moved, copied,
 * removed; the song) and the section page's (OCT+: the selected track's sound in it, its punches, a new letter of
 * it). Included by ui_popup.c where upstream's song sheets were; those (and the jam's) stay there verbatim under
 * MELODEE_UPSTREAM_SONG, never defined: tools/upstream_shadow.py lists upstream's changes to them */
static void ps_dup(void) { if (ps_song_free()) song_insert(0); }          /* the same section again, after it */
static void ps_copy_new(void) { if (ps_song_free()) song_insert(1); }     /* a new letter with the same contents */
/* SONG, OCT+ held: the column picked (its section placed again, a new letter of it, moved, copied, removed), the song */
static const sheet_row_t SHEET_SONG[] = {
    {"Duplicate section", 0, 0, ps_dup, 0}, {"Copy as new section", 0, 0, ps_copy_new, 0},
    {"Move section", 0, 0, song_sections_move, 0}, {"Copy section", 0, 0, song_sections_copy, 0},
    {"Delete section", 0, 0, 0, CF_DEL_ROW},
    {"Loop song", ps_song_loop_v, ps_song_loop_turn, ps_song_loop, 0},
    {"Clear song", 0, 0, 0, CF_CLEAR_SONG}};
/* the section page, OCT+: the selected track's sound in it, its punches, a new letter of it (its length: SELECT) */
static void ps_sect_trk_v(char *b) { str_cpy(b, "T1", 3); b[1] = (char)('1' + song.sel % NTRK); }
static const sheet_row_t SHEET_SECTION[] = {
    {"Set preset", ps_sect_trk_v, 0, song_preset_open, 0}, {"Remove preset", ps_sect_trk_v, 0, song_preset_remove, 0},
    {"Punch FX", 0, 0, song_punch_open, 0}, {"Copy as new section", 0, 0, ps_copy_new, 0}};
static void sect_sheet_open(void)
{
    char t[12] = "Section A", b[16] = "in song ";
    t[8] = (char)('A' + chain_config.sect[sp.row]);
    fmt_int(b + 8, sect_uses(chain_config.sect[sp.row])); str_cpy(b + str_len(b), "x", 2);
    sheet_open(t, b, SHEET_SECTION, NELEM(SHEET_SECTION));
}
