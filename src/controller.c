#include "controller.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

volatile dc_controller_state_t g_controller_state;
spin_lock_t *g_controller_spinlock;
volatile uint8_t   g_rumble_intensity;
volatile ctrl_mode_t g_ctrl_mode;

void controller_state_init(void) {
    g_controller_spinlock = spin_lock_init(CONTROLLER_SPINLOCK_ID);
    g_rumble_intensity = 0;
    g_ctrl_mode = CTRL_MODE_STANDARD;

    dc_controller_state_t neutral = DC_STATE_NEUTRAL;
    memcpy((void *)&g_controller_state, &neutral, sizeof(neutral));
}

void controller_state_update(const dc_controller_state_t *state) {
    uint32_t saved = spin_lock_blocking(g_controller_spinlock);
    memcpy((void *)&g_controller_state, state, sizeof(dc_controller_state_t));
    spin_unlock(g_controller_spinlock, saved);
}

void controller_state_read(dc_controller_state_t *dst) {
    uint32_t saved = spin_lock_blocking(g_controller_spinlock);
    memcpy(dst, (const void *)&g_controller_state, sizeof(dc_controller_state_t));
    spin_unlock(g_controller_spinlock, saved);
}

void controller_set_rumble(uint8_t intensity) {
    g_rumble_intensity = intensity;
}

void controller_set_mode(ctrl_mode_t mode) {
    if (mode >= CTRL_MODE_COUNT) return;
    g_ctrl_mode = mode;
    printf("[ctrl] mode -> %u\n", (unsigned)mode);
}

ctrl_mode_t controller_get_mode(void) {
    return g_ctrl_mode;
}
