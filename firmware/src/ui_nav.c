/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Header and navigation (task.md): Bluetooth changes as brief toasts, its status in the header only on the Bluetooth
 * page (ui_draw.c ble_head_text). Included at the end of ui_input.c */

#if MELODEE_BLE
static struct { uint8_t ready, failed; } nav_ble __attribute__((section(".pool")));
/* main loop, after mel_ble_poll: connected, disconnected, a failure; the Bluetooth page shows them itself */
static void nav_ble_toasts(void)
{
    uint8_t ready = (uint8_t)(mb.on && mb.ready), failed = (uint8_t)(mb.failure[0] != 0);
    if (ui.menu != 3u) {
        if (ready != nav_ble.ready) ui_message(ready ? "BLE CONNECTED" : "BLE DISCONNECTED");
        else if (failed && !nav_ble.failed) ui_message(mb.failure);
    }
    nav_ble.ready = ready;
    nav_ble.failed = failed;
}
#endif
