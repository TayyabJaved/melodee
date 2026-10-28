/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* count columns, column r every track's bank r (MIDI-only: of 8), as sections: identical columns share a letter */
static void arrangement(uint32_t count)
{
    chain_defaults(&chain_config); chain_config.count = (uint8_t)count;
    memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < count; r++) {
        for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = (uint8_t)(r % trk_npat(k));
        chain_config.row[r] = (chain_row_t){r % NPAT, 1};
    }
    (void)song_sections_normalize(1);
}
static void song_page(void) { go_page(GR_SONG); frame(); }
static void select_range(int32_t delta)
{
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= 1u << panel.btn[B_OCTUP]; frame();
    turn(EN_K1, delta);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]); frame();
}
static void one_step_tracks(void)                       /* every part: a 1-step pattern, a note */
{
    for (uint32_t k = 0; k < NPART; k++) {
        trk[k].p[P_SLEN] = 1;
        trk[k].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96};
        pattern_commit(&trk[k]);
    }
}
static int cursor_playback(void)
{
    int bad = 0; ui_power_on(); one_step_tracks();
    arrangement(64); memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < CHAIN_ROWS; r++) { chain_config.row[r].slot = 0; chain_config.sect[r] = 0; }
    chain_config.sect[62] = 1; chain_config.len[62] = 2;   /* (B: two sixteenths, the 1-step patterns twice) */
    song_page(); ui.song_row = 62;
    press(B_PLAY);
    bad += check("SONG PLAY snapshots the selected section before the ISR starts", chain.armed && chain.start == 62);
    turn(EN_K1, -10); pattern_poll(); events_block(1);
    bad += check("SONG starts the selected section at step zero", chain.running && chain.row == 62 && trk[0].seq_idx == 0 && trk[0].seq_n == 1);
    uint32_t period = seq_div_samples(trk[0].p[P_SDIV]);
    events_block(period);
    bad += check("cursor movement does not seek; a shorter pattern loops inside its section", chain.row == 62 && ui.song_row == 52 && trk[0].seq_idx == 0);
    events_block(period);
    bad += check("cursor playback continues into the following column", chain.row == 63 && chain.running);
    events_block(period);
    bad += check("cursor playback stops at the end and restores the original bank", !chain.running && !song.playing && trk[0].pattern == 0 && trk[0].step[0].note[0] == 60);
    ui.song_row = 20; press(B_OCTUP);
    bad += check("OCT+ on a column opens its section page and never starts playback", sect_on() && sp.row == 20 && !chain.armed && !song.playing);
    press(B_PLAY); events_block(1);
    bad += check("PLAY on the section page loops that section", chain.running && chain.solo && chain.row == 20);
    events_block(3u * period);
    bad += check("  again and again, never the next column", chain.running && chain.row == 20);
    press(B_PLAY); events_block(1); press(B_OCTDN);
    bad += check("  PLAY stops it, OCT- returns to SONG", !chain.running && !sect_on() && cur_page()->graph == GR_SONG);
    ui.song_row = 63; press(B_PLAY); press(B_PLAY); events_block(1);
    bad += check("PLAY can cancel a pending cursor start", !chain.armed && !chain.running && !song.playing);
    arrangement(2); ui.song_row = chain_config.count; press(B_PLAY); events_block(1);
    bad += check("PLAY on the add-section column starts at section one", chain.running && !chain.row);
    seq_stop();
    return bad;
}
static int rests(void)
{
    int bad = 0; ui_power_on();
    track_t *t = &trk[0]; t->p[P_SLEN] = 1;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96}; pattern_commit(t);
    motion_set_event(t, 0, P_LEVEL, 12);
    recording[0] = (recorded_note_t){.duration=RECORD_UNIT,.note7=72,.vel=96};
    recorded_set_owner(&recording[0], recording_owner(t)); recording_reindex();
    arrangement(3); memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < 3; r++) { chain_config.row[r].slot = 0; chain_config.sect[r] = (uint8_t)r; }
    chain_patterns[0][0] = CHAIN_REST; chain_config.row[0].slot = CHAIN_REST;
    chain_patterns[1][1] = CHAIN_REST;
    chain_config.len[0] = 32;                           /* (A: two bars) */
    int16_t level = t->p[P_LEVEL];
    bad += check("a song may start with a rest on track one", !chain_prepare_from(0));
    events_block(1);
    bad += check("REST suppresses manual notes, recorded notes and motion", chain.running && !t->seq_n && t->p[P_LEVEL] == level && !t->rh_n);
    uint32_t bar = 16u * seq_div_samples(2u);
    events_block(bar - 1u); events_block(1);
    bad += check("a resting track one holds its section for the section's whole length", !chain.row && !t->seq_n);
    events_block(bar);
    bad += check("leaving a rest resumes the referenced pattern and motion", chain.row == 1 && t->p[P_LEVEL] == 12 && t->seq_n && !trk[1].seq_n);
    seq_stop();
    bad += check("STOP after rests restores timing, sound controls and pattern contents", t->p[P_SLEN] == 1 && t->step[0].note[0] == 60 && t->p[P_LEVEL] == level);
    arrangement(1); memset(chain_patterns[0], CHAIN_REST, NTRK); chain_config.row[0].slot = CHAIN_REST;
    bad += check("an entirely resting section is a bar long (AUTO)", sect_len_u(0) == 96u);
    chain_prepare(); events_block(1); events_block(bar);
    bad += check("an entirely resting song advances and stops normally", !song.playing && !chain.running);
    return bad;
}
static int lengths(void)
{
    int bad = 0; ui_power_on();
    trk[0].p[P_SLEN] = 1; trk[0].step[0] = (step_t){{60},1,ST_NOTE,0,96}; pattern_commit(&trk[0]);
    trk[1].p[P_SLEN] = 4;
    trk[1].step[0] = (step_t){{64},1,ST_NOTE,0,96};
    for (uint32_t j = 1; j < 4; j++) trk[1].step[j] = (step_t){{0},0,ST_TIE};
    pattern_commit(&trk[1]); motion_set_event(&trk[1], 2, P_LEVEL, 12);
    for (uint32_t k = 2; k < NPART; k++) { trk[k].p[P_SLEN] = 1; pattern_commit(&trk[k]); }
    arrangement(2); memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < 2; r++) { chain_config.row[r].slot = 0; chain_config.sect[r] = 0; }
    bad += check("AUTO: the section is its longest pattern (track 2's four steps)", sect_len_u(0) == 4u * 6u);
    trk[1].p[P_SLEN] = 3; pattern_commit(&trk[1]);
    bad += check("  and follows that pattern's length until one is set", sect_len_u(0) == 3u * 6u);
    trk[1].p[P_SLEN] = 4; pattern_commit(&trk[1]);
    uint32_t period = seq_div_samples(trk[0].p[P_SDIV]);
    chain_prepare(); events_block(1);
    events_block(period);
    bad += check("track 1's shorter pattern loops; track 2 holds its note", !chain.row && !trk[0].seq_idx && trk[1].seq_idx == 1 && trk[1].seq_n == 1);
    events_block(period);
    bad += check("automation follows each pattern's own phase", trk[1].seq_idx == 2 && trk[1].p[P_LEVEL] == 12);
    events_block(period); events_block(period);
    bad += check("the next column (A again): every track from its step 0, even the same pattern", chain.row == 1 && !trk[1].seq_idx && trk[1].seq_n == 1);
    seq_stop();
    chain_config.len[0] = chain_config.len[1] = 2;      /* (two sixteenths: track 2 cut) */
    bad += check("a set length shorter than a pattern cuts it (the section page marks it)", sect_cut(0, 1) && !sect_cut(0, 0));
    chain_prepare(); events_block(1); events_block(period); events_block(period);
    bad += check("  the next column starts after two steps, track 2 from its top", chain.row == 1 && !trk[1].seq_idx);
    seq_stop();
    arrangement(1); memset(chain_patterns[0], CHAIN_REST, NTRK); chain_config.row[0].slot = CHAIN_REST;
    trk[0].p[P_SDIV] = 2; song.g[G_BPM] = 120;
    char time[16]; sg_time_text(time, chain_row_samples(0));
    bad += check("the song's time shows minutes and seconds (a resting bar at 120: 0:02)", str_eq(time, "0:02"));
    char len[24], brief[16];
    chain_config.len[0] = 56; sect_len_text(len, 0, 0); sect_len_text(brief, 0, 1);
    bad += check("lengths read as bars and beats (56 sixteenths: 3 bars 2 beats, briefly 3:2)", str_eq(len, "3 bars 2 beats") && str_eq(brief, "3:2"));
    chain_config.len[0] = 8; sect_len_text(len, 0, 0);
    bad += check("  8 sixteenths: 2 beats", str_eq(len, "2 beats"));
    uint32_t remainder = 0; uint64_t long_time = UINT64_C(12345678901);
    uint32_t quotient = chain_div_samples(long_time, 50321u, &remainder);
    bad += check("long-song sample arithmetic retains its full timeline precision", quotient == long_time / 50321u && remainder == long_time % 50321u);
    return bad;
}
static int edits(void)
{
    int bad = 0; ui_power_on(); arrangement(4); song_page(); ui.song_row = 1;
    bad += check("four different columns: four sections (A B C D)", chain_config.sect[1] == 1 && chain_config.sect[3] == 3);
    turn(EN_K2, 1);
    bad += check("KNOB 2: the column plays the next section (C), B gone with its last column", chain_config.sect[1] == 2 &&
                 chain_patterns[1][0] == 2 && !sect_uses(1));
    hold(B_EDIT);
    bad += check("EDIT undoes it", chain_config.sect[1] == 1 && chain_patterns[1][0] == 1);
    edit_redo();
    bad += check("EDIT plus OCT+ redoes it", chain_config.sect[1] == 2);
    hold(B_EDIT);
    song_column_put(3, 0, 0); song_edit_sync();         /* (A B C A) */
    ui.song_row = 0; press(B_OCTUP); turn(EN_K2, 1);
    bad += check("the section page edits every column of its section at once", sect_on() && chain_patterns[0][1] == 1 && chain_patterns[3][1] == 1 &&
                 chain_patterns[1][1] == 1 && chain_patterns[2][1] == 2);
    turn(EN_K2, -3);
    bad += check("  below P1: a rest, in both", chain_patterns[0][1] == CHAIN_REST && chain_patterns[3][1] == CHAIN_REST);
    press(B_OCTDN); hold(B_EDIT); hold(B_EDIT);
    bad += check("  undone, a step at a time", !chain_patterns[0][1] && !chain_patterns[3][1]);
    ui.song_row = 0; hold(B_OCTUP); sheet_do("Copy as new section");
    bad += check("Copy as new section: the next free letter (D: A in column 4 took its place), the same contents, after it",
                 chain_config.count == 5 && chain_config.sect[1] == 3 && !memcmp(chain_patterns[0], chain_patterns[1], NTRK));
    ui.song_row = 1; hold(B_OCTUP); sheet_do("Duplicate section");
    bad += check("Duplicate section: the same section again after it", chain_config.count == 6 && chain_config.sect[2] == 3);
    hold(B_EDIT); hold(B_EDIT);
    arrangement(4); song_edit_sync(); ui.song_row = 1;
    select_range(1);
    bad += check("held OCT+ and KNOB 1 select a range without starting transport", se.range && song_range_first() == 1 && song_range_last() == 2 && !pop.on && !song.playing);
    press(B_OCTUP); sheet_do("Copy sections"); turn(EN_K1, 2); press(B_OCTUP);
    bad += check("range copy places whole sections (their letters) before the destination", chain_config.count == 6 && chain_patterns[4][0] == 1 &&
                 chain_patterns[5][0] == 2 && chain_patterns[3][0] == 3 && chain_config.sect[4] == 1 && chain_config.sect[5] == 2);
    hold(B_EDIT);
    bad += check("one undo restores the complete phrase before a range operation", chain_config.count == 4 && chain_patterns[3][0] == 3);
    edit_redo();
    ui.song_row = 1; select_range(1); press(B_OCTUP); sheet_do("Move sections"); turn(EN_K1, 4); press(B_OCTUP);
    bad += check("range move preserves order and removes the original phrase", chain_config.count == 6 && chain_patterns[0][0] == 0 && chain_patterns[1][0] == 3 && chain_patterns[4][0] == 1 && chain_patterns[5][0] == 2);
    ui.song_row = 0; select_range(1); press(B_OCTUP); sheet_do("Delete sections");
    bad += check("range delete closes the gap and is undoable", chain_config.count == 4);
    hold(B_EDIT);
    bad += check("range-delete undo restores all removed sections", chain_config.count == 6 && chain_patterns[0][0] == 0 && chain_patterns[1][0] == 3);
    arrangement(CHAIN_ROWS); song_edit_sync(); ui.song_row = 0;
    song_sections_move(); turn(EN_K1, CHAIN_ROWS); song_apply_destination();
    bad += check("a full song can move its first column after the final column", chain_config.count == CHAIN_ROWS && chain_patterns[0][0] == 1 && chain_patterns[CHAIN_ROWS-1u][0] == 0 && ui.song_row == CHAIN_ROWS-1u);
    se.range = 1; se.anchor = 0; se.end = CHAIN_ROWS-1u; song_sections_delete();
    bad += check("selecting and deleting an entire 64-column song is bounded", !chain_config.count);
    song_edit_history(0);
    bad += check("undo restores an entire deleted 64-column song", chain_config.count == CHAIN_ROWS && chain_patterns[CHAIN_ROWS-1u][0] == 0);
    resource_release(RES_SONG_UNDO); song_edit_history(0);
    bad += check("reclaimed history refuses undo without touching the song", chain_config.count == CHAIN_ROWS && msg_is("NOTHING TO UNDO"));
    return bad;
}
static int adding(void)
{
    int bad = 0; ui_power_on(); song_page();
    for (uint32_t r = 0; r < SONG_SECTIONS; r++) {      /* (26 different combinations: A..Z) */
        pattern_request(&trk[0], r % 16u); pattern_request(&trk[1], r / 16u);
        ui.song_row = chain_config.count; press(B_OCTUP);
    }
    bad += check("OCT+ on +: a new letter for each new combination, up to Z", chain_config.count == SONG_SECTIONS &&
                 chain_config.sect[SONG_SECTIONS - 1u] == SONG_SECTIONS - 1u);
    pattern_request(&trk[0], 15); pattern_request(&trk[1], 1); press(B_OCTUP);
    bad += check("  a 27th combination is refused", chain_config.count == SONG_SECTIONS && msg_is("26 SECTIONS IN USE"));
    pattern_request(&trk[0], 3); pattern_request(&trk[1], 0); press(B_OCTUP);
    bad += check("  a combination already there places its section again", chain_config.count == SONG_SECTIONS + 1u &&
                 chain_config.sect[SONG_SECTIONS] == 3u && msg_is("PLACED D"));
    ui.song_row = 3; hold(B_OCTUP); sheet_do("Delete section"); press(B_OCTUP);
    bad += check("Delete section removes the column only; its section lives on in its other column", chain_config.count == SONG_SECTIONS &&
                 sect_uses(3) == 1u);
    uint32_t id = sect_free();
    bad += check("  removing a section's last column frees its letter", id == SONG_SECTIONS);
    ui.song_row = 3; hold(B_OCTUP); sheet_do("Delete section"); press(B_OCTUP);
    bad += check("  (E, now in column 4: its only column gone, its letter free)", sect_free() == 4u);
    return bad;
}
static int recorded_notes(void)
{
    int bad=0; ui_power_on(); one_step_tracks(); trk[1].p[P_SLEN]=8; pattern_commit(&trk[1]);
    recorded_note_t note={.duration=32768,.note7=72,.vel=96};
    recorded_set_exponent(&note,3); recorded_set_owner(&note,recording_owner(&trk[1]));
    recording_restore_note(&trk[1],0,note); recording_reindex();
    trk[1].step[0] = (step_t){{72},1,ST_NOTE,SF_RECORDED,96};
    arrangement(3); memset(chain_patterns,0,sizeof chain_patterns);
    for(uint32_t r=0;r<3;r++){chain_config.row[r].slot=0;chain_config.sect[r]=0;chain_config.len[r]=2;}
    chain_prepare();events_block(1);uint32_t period=seq_div_samples(trk[0].p[P_SDIV]);
    events_block(period);
    bad+=check("a recorded note sounds through its section",!chain.row&&recording_owns(&trk[1],72));
    events_block(period);
    bad+=check("the section's end releases it; the next column plays it again from the top",chain.row==1&&!trk[1].seq_idx&&recording_owns(&trk[1],72));
    seq_stop();return bad;
}
static int song_looping(void)
{
    int bad=0;ui_power_on();
    bad+=check("song loop defaults off",!chain_config.rsv[CHAIN_LOOP]);
    one_step_tracks();
    pattern_request(&trk[0],2);trk[0].p[P_SLEN]=7;trk[0].step[0]=(step_t){{75},1,ST_NOTE,0,96};
    arrangement(2);memset(chain_patterns,0,sizeof chain_patterns);chain_config.row[1].slot=0;
    chain_config.sect[1]=1;chain_config.len[0]=2;   /* (A: two sixteenths, B: AUTO, one) */
    song_page();hold(B_OCTUP);sheet_do("Loop song");
    bad+=check("held OCT+ offers Loop song with a persistent header state",chain_config.rsv[CHAIN_LOOP]&&str_eq(song_edit_status(),"LOOP"));
    chain_prepare_from(1);events_block(1);uint32_t period=seq_div_samples(trk[0].p[P_SDIV]);
    trk[0].seq_pos=period-8u+3u;chain.rest_pos=chain.len-8u+3u;events_block(8);
    bad+=check("a cursor start loops the complete song with fractional timing intact",chain.running&&song.playing&&!chain.row&&trk[0].seq_pos==3);
    events_block(period);bad+=check("looping plays the first section's whole length",!chain.row);
    events_block(period);events_block(period);
    bad+=check("song looping survives multiple passes",chain.running&&!chain.row);
    song_loop_set(0);bad+=check("changing loop mode while playing is refused",chain.config.rsv[CHAIN_LOOP]&&chain_config.rsv[CHAIN_LOOP]&&msg_is("STOP TO EDIT"));
    press(B_PLAY);events_block(1);
    bad+=check("PLAY stops a loop and restores the original pattern and timing",!chain.running&&!song.playing&&trk[0].pattern==2&&trk[0].p[P_SLEN]==7&&trk[0].step[0].note[0]==75);
    bad+=check("loop mode saves with the project",!project_save(0));
    song_loop_set(0);project_load(0);
    bad+=check("project reload restores loop mode",chain_config.rsv[CHAIN_LOOP]==1);
    uint8_t *raw=proj_bank_slot[0];raw[BANKP_STEPS_OFF+1u]=2;bank_checksum(raw);
    bad+=check("invalid saved loop values are rejected before restoration",!bank_valid(raw,BANK_STORE_SIZE));
    song_loop_set(0);project_save(1);song_loop_set(1);project_load(1);
    bad+=check("ordinary older project records default to non-looping",!chain_config.rsv[CHAIN_LOOP]);
    arrangement(1);memset(chain_patterns[0],CHAIN_REST,NTRK);chain_config.row[0].slot=CHAIN_REST;song_loop_set(1);
    chain_prepare();events_block(1);uint32_t bar=16u*seq_div_samples(2u);chain.rest_pos=bar-8u+2u;events_block(8);
    bad+=check("a single resting section loops without hanging or losing time",chain.running&&!chain.row&&chain.rest_pos==2&&!trk[0].seq_n);
    seq_stop();return bad;
}
static int persistence(void)
{
    int bad = 0; ui_power_on(); arrangement(64);
    chain_patterns[47][2] = CHAIN_REST; chain_patterns[63][2] = CHAIN_REST;   /* (column 15's section: rests too) */
    chain_patterns[15][2] = chain_patterns[31][2] = CHAIN_REST;
    chain_patterns[22][0] = chain_patterns[6][0] = chain_patterns[38][0] = chain_patterns[54][0] = CHAIN_REST;
    for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
        chain_config.row[r].slot = chain_patterns[r][0];
        chain_config.len[r] = (uint16_t)(r % 16u * 4u);
    }
    bad += check("64 columns of 16 sections with rests and lengths are consistent", sect_consistent());
    chain_config_t expected = chain_config;
    uint8_t patterns[CHAIN_ROWS][NTRK]; memcpy(patterns, chain_patterns, sizeof patterns);
    bad += check("64 columns save successfully", !project_save(0));
    uint32_t magic; memcpy(&magic, proj_bank_slot[0], 4);
    bad += check("extended songs save in the 16-pattern container", magic == BANKP_MAGIC);
    memcpy(&magic, proj_bank_slot[0] + BANK_SECT_OFF, 4);
    bad += check("  with the sections' block (SEC1)", magic == BANK_SECT_MAGIC);
    pattern_init(); chain_defaults(&chain_config); project_load(0);
    bad += check("all 64 columns, letters, lengths and rests survive reload", !memcmp(&expected, &chain_config, sizeof expected) && !memcmp(patterns, chain_patterns, sizeof patterns));
    uint8_t *raw = proj_bank_slot[0]; uint32_t offset = BANKP_STEPS_OFF + 5u + 63u * (NTRK + 1u) + 2u;
    uint8_t keep = raw[offset];
    raw[offset] = CHAIN_HOLD + 1u; bank_checksum(raw);
    bad += check("malformed extended assignments are rejected even with a valid checksum", !bank_valid(raw, BANK_STORE_SIZE));
    raw[offset] = keep; raw[BANK_SECT_OFF + 4u] = SONG_SECTIONS; bank_checksum(raw);
    bad += check("a letter past Z is rejected", !bank_valid(raw, BANK_STORE_SIZE));
    raw[BANK_SECT_OFF + 4u] = 0; raw[BANK_SECT_OFF + 4u + CHAIN_ROWS + 1u] = 0xff; bank_checksum(raw);
    bad += check("a length past 64 bars is rejected", !bank_valid(raw, BANK_STORE_SIZE));
    arrangement(2); bad += check("ordinary songs save too", !project_save(1));
    memcpy(&magic, proj_bank_slot[1], 4);
    bad += check("ordinary songs save in the same container (FBKK only as a dense fallback)", magic == BANKP_MAGIC);
    return bad;
}
/* A record from before sections (no SEC1): repeats become lengths, held blocks give their columns the pattern (they
 * restart there), identical columns share a letter; past 26 sections the song ends there. */
static int older_songs(void)
{
    int bad = 0; ui_power_on(); one_step_tracks();
    chain_defaults(&chain_config); chain_config.count = 4; memset(chain_patterns, 0, sizeof chain_patterns);
    chain_config.row[0] = (chain_row_t){0, 3};
    chain_config.row[1] = (chain_row_t){0, 1};
    chain_config.row[2] = (chain_row_t){0, 1}; chain_patterns[2][1] = 1;
    chain_config.row[3] = (chain_row_t){0, 1}; chain_patterns[3][1] = CHAIN_HOLD;
    bad += check("an older song saves", !project_save(0));
    uint8_t *raw = proj_bank_slot[0]; memset(raw + BANK_SECT_OFF, 0, BANK_SECT_BYTES); bank_checksum(raw);
    bad += check("  as an older record would be (no SEC1)", bank_valid(raw, BANK_STORE_SIZE) && !bank_sect);
    song_note = 0; pattern_init(); chain_defaults(&chain_config); project_load(0);
    bad += check("loaded: 3 repeats of track 1's step are a 3-sixteenth section", chain_config.len[0] == 3u && chain_config.row[0].repeat == 1u);
    bad += check("  AUTO where that was its longest pattern", !chain_config.len[1]);
    bad += check("  a held block gives its column the pattern", chain_patterns[3][1] == 1u);
    bad += check("  identical columns share a letter (A B C C)", chain_config.sect[0] == 0u && chain_config.sect[1] == 1u &&
                 chain_config.sect[2] == 2u && chain_config.sect[3] == 2u);
    bad += check("  the load says blocks now restart", (song_note & SN_HELD) != 0u);
    frame();
    bad += check("  (the message, once)", msg_is("SONG BLOCKS RESTART") && !song_note);
    chain_defaults(&chain_config); chain_config.count = 30; memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < 30u; r++) { chain_patterns[r][0] = (uint8_t)(r % 16u); chain_patterns[r][1] = (uint8_t)(r / 16u); chain_config.row[r] = (chain_row_t){(uint8_t)(r % 16u), 1}; }
    uint32_t note = song_sections_normalize(0);
    bad += check("30 different older rows: the song ends at the 26th, and says so", chain_config.count == SONG_SECTIONS && (note & SN_CUT) &&
                 chain_valid(&chain_config));
    return bad;
}
int main(void)
{
    int bad = cursor_playback() + rests() + lengths() + edits() + adding() + recorded_notes() + song_looping() + persistence() + older_songs();
    printf("Song mode: %s\n", bad ? "FAILED" : "passed"); return !!bad;
}
