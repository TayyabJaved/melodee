/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SHAPE arp patterns through the device's FLASH path (project_stream.c), the RAM codec its oracle: a song with its own
 * shapes streams the FBKP record bank_pack makes (SHP1 in it), a new blank song has the defaults, loading brings the
 * shapes back and saving again gives the same SHP1 block; a song with the default shapes carries them too.
 * Run by tests/run_tests.sh. */
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
#include <assert.h>
static uint8_t oracle[BANK_STORE_SIZE], actual[BANK_STORE_SIZE];
/* a slot's current record as the loader reads it (a compact image expanded, its holes zeros: as
 * tests/project_stream_test.c ps_saved) -> 0 */
static int saved(uint32_t obj, uint8_t *dst)
{
    st_hdr_t h; int copy = st_current(obj, &h);
    if (copy < 0) return -1;
    uint8_t on = pj.on; pj_head_t ph = pj.h;
    pj.on = !st_read_range(obj, (uint32_t)copy, 0, &pj.h, PJ_HEAD) && pj.h.magic == PJ_MAGIC;
    int rc = pj_read(obj, (uint32_t)copy, 0, dst, BANK_STORE_SIZE);
    pj.on = on; pj.h = ph; return rc;
}
static int save_slot(uint32_t s) { song.g[G_SLOT] = (int16_t)(s + 1u); return project_save_as(s, 0); }
static void open_slot(uint32_t s) { song.g[G_SLOT] = (int16_t)(s + 1u); project_load(s); }

int main(void)
{
    uint32_t magic, shp, hash;
    reset();
    for (uint32_t k = 0; k < NTRK; k++) shape_default(k);
    shape.ctrl[1][0] = SD_UPDN; shape.ctrl[1][1] = 12; shape.mask[1] = 0x5A5u;
    project_capture(&proj_scratch); assert(bank_pack(oracle, &proj_scratch, 1));
    memcpy(&magic, oracle, 4); memcpy(&shp, oracle + BANK_SHAPE_OFF, 4);
    assert(bank_wide && magic == BANKP_MAGIC && shp == BANK_SHAPE_MAGIC);
    project_capture(&proj_scratch);
    assert(proj_image_hash(1, &hash) && hash == proj_hash(oracle, sizeof oracle));
    assert(!save_slot(0)); assert(!project_dirty());
    assert(!saved(OBJ_BANK0, actual) && !memcmp(actual, oracle, sizeof actual));
    project_new_blank();
    assert(shape.ctrl[1][1] == SHAPE_DEF_STEPS && shape.mask[1] == ~(uint64_t)0);   /* (a new song: the default shapes) */
    open_slot(0);
    assert(shape.ctrl[1][0] == SD_UPDN && shape.ctrl[1][1] == 12 && shape.mask[1] == 0x5A5u);
    project_capture(&proj_scratch); assert(bank_pack(oracle, &proj_scratch, 1));   /* (saved again: the same SHP1) */
    assert(!memcmp(oracle + BANK_SHAPE_OFF, actual + BANK_SHAPE_OFF, BANK_SHAPE_BYTES));

    reset();
    for (uint32_t k = 0; k < NTRK; k++) shape_default(k);
    assert(!save_slot(1));
    assert(!saved(OBJ_BANK0 + 1u, actual));
    memcpy(&magic, actual, 4); memcpy(&shp, actual + BANK_SHAPE_OFF, 4);
    {
        uint8_t d[SHAPE_BYTES]; shape_raw_default(d);
        assert(magic == BANKP_MAGIC && shp == BANK_SHAPE_MAGIC && !memcmp(actual + BANK_SHAPE_OFF + 4u, d, sizeof d));
    }
    puts("SHAPE stream: own shapes stream in FBKP's SHP1 and load back after a new song; defaults stored as defaults: OK");
    return 0;
}
