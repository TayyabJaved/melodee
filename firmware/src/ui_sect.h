/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Song sections' SONG editing (ours): what ui_song.c's upstream code needs to see of ui_sect.c, which is included at
 * its end. Included once, by ui_song.c */
/* the section page: open over SONG, the column it shows (its section is edited in every column of it) */
static struct { uint8_t open, row; } sp;
static int sect_on(void);
static int sect_looping(void);
static int sect_edit_busy(void);
static void sect_edit_end(void);
static void sect_rows_changed(void);
static void sect_rows_remove(uint32_t first, uint32_t n, uint32_t tail);
static void song_arm(uint32_t mode);                   /* (upstream's replaced: song_sections_copy / _move call it) */
static void song_insert(uint32_t fresh);
static void song_apply_destination(void);
static const char *song_edit_status(void);
