#ifndef SUBSPACE_SIGN_CLOCK_H
#define SUBSPACE_SIGN_CLOCK_H

#include <time.h>
#include <user_interface.h>

/* --- Types --- */
struct clock_context {
    uint32_t *led_buf;
    struct tm prev_tm;
};

/* --- Functions --- */
/**
 * Initialize the given context and the SNTP module.
 *
 * @param ctx the clock context.
 * @param led_buf the buffer to write to on updates.
 * @param led_buf_size the number of LEDs. Must be 120.
 * @return true on success.
 */
extern bool ICACHE_FLASH_ATTR clock_init(struct clock_context *ctx, uint32_t *led_buf, uint8_t led_buf_size);

/**
 * Check whether the realtime clock is valid.
 *
 * @param ctx the clock context.
 * @return true if calling clock_update will produce something useful.
 */
extern bool ICACHE_FLASH_ATTR clock_is_valid(struct clock_context *ctx);

/**
 * Update the LED buffer with the current time.
 *
 * @param ctx the clock context.
 */
extern void ICACHE_FLASH_ATTR clock_update(struct clock_context *ctx);

#endif /* SUBSPACE_SIGN_CLOCK_H */
