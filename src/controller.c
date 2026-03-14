#include "controller.h"
#include "config.h"
#include <string.h>

volatile dc_controller_state_t g_controller_state;
spin_lock_t *g_controller_spinlock;

void controller_state_init(void) {
    g_controller_spinlock = spin_lock_init(CONTROLLER_SPINLOCK_ID);

    // All buttons released, sticks centred
    g_controller_state.buttons       = DC_BUTTONS_RELEASED;
    g_controller_state.right_trigger = DC_TRIGGER_RELEASED;
    g_controller_state.left_trigger  = DC_TRIGGER_RELEASED;
    g_controller_state.joy_x         = DC_STICK_CENTER;
    g_controller_state.joy_y         = DC_STICK_CENTER;
    g_controller_state.joy2_x        = DC_STICK_CENTER;
    g_controller_state.joy2_y        = DC_STICK_CENTER;
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
