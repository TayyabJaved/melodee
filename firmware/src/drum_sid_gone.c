/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT SID is removed in this fork (DEV.md row 14), drum_sid.c with it; upstream's drum_lanes.c (CR78, FM) still names
 * two of its pieces, so they stand in here and drum_lanes.c stays upstream's. Included by eng_drum.c before it.
 * dsk_level is drum_sid.c's as of upstream 1.6.0-rc.6: at a replay, compare (git show <tag>:firmware/src/drum_sid.c) */
typedef struct dsk_part { uint8_t gone; } dsk_part_t;   /* (only its size, for drum_lanes.c's assert: kept distinct) */
static int32_t dsk_level(const int16_t *p, uint32_t lane, uint32_t level)   /* a lane's LEVEL, squared */
{
    int32_t lv = clamp(p[P_LN0 + (lane & (DV_NLANE - 1u))], 0, 127) * (int32_t)level / 127;
    return lv * lv * 32767 / (127 * 127);
}
