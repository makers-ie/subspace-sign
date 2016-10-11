#include "ws2811-esp8266.h"

#include <gpio.h>
#include <osapi.h>

/* --- Macros --- */
#define TIMER1_DIVIDE_BY_1 0x0000
#define TIMER1_DIVIDE_BY_16 0x0004
#define TIMER1_DIVIDE_BY_256 0x000C

#define TIMER1_AUTO_LOAD 0x0040
#define TIMER1_ENABLE_TIMER 0x0080
#define TIMER1_FLAGS_MASK 0x00CC
#define TIMER1_COUNT_MASK 0x007FFFFF

// 2500 ns is too fast even for a simple pulser at 80MHz.
// https://wp.josh.com/2014/05/13/ws2812-neopixels-are-not-so-finicky-once-you-get-to-know-them/
// indicates up to 6000 ns is acceptable.
// 3250 ns causes skipped ticks.
// 3500 ns seems stable.
#define WS2811_TBIT 3500  // ns
#define WS2811_TRES 50000 // >=50 us

/* --- Data --- */
/**
 * Initialized WS2811 contexts.
 * These are used in the interrupt handler.
 */
static struct ws2811_context *ws2811_intr_ctxs[WS2811_MAX_NUM_CONTEXTS + 1];

/* --- Functions --- */
extern void ets_isr_attach(int, void (*)(void *), void *);
extern void ets_isr_mask(uint32_t);
extern void ets_memset(void *, uint8_t, int);
extern void NmiTimSetFunc(void (*)(void));

/**
 * Arm the timer1 module with the given number of ticks.
 */
static inline void ws2811_timer_arm(uint32_t ticks) { RTC_REG_WRITE(FRC1_LOAD_ADDRESS, ticks); }

/**
 * Return the number of ticks needed for the timer1 module until time t.
 *
 * @param t The time, in nanoseconds.
 * @param div The current timer divisor. One of 1, 16 or 256.
 * @return The number of ticks.
 */
static inline uint32_t ws2811_ns_to_rtc_timer_ticks(uint32_t t, uint32_t div) {
    if (!t)
        return 0;
    if (t > 0xFFFFFFFF / (APB_CLK_FREQ / div)) {
        uint32_t u = t % 1024;
        return t / 1024 * APB_CLK_FREQ / div / (1000000000 / 1024) + u / 64 * APB_CLK_FREQ / div / (1000000000 / 64) +
               (u % 64) * APB_CLK_FREQ / div / 1000000000;
    }
    return t * APB_CLK_FREQ / div / 1000000000;
}

/**
 * NMI interrupt handler.
 *
 * Note this must not use ICACHE_FLASH_ATTR code.
 */
static void ws2811_tx_intr(void) {
#if WS2811_MAX_NUM_CONTEXTS == 1
    {
        struct ws2811_context **ctxp = ws2811_intr_ctxs;
#else
    for (struct ws2811_context **ctxp = ws2811_intr_ctxs; *ctxp; ++ctxp) {
#endif
#define ctx (*ctxp)

        switch (ctx->state) {
        case WS2811_STATE_BIT: {
            // Send the next bit.
            // We start by clearing the signal. This negative edge has no impact.
            GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, ctx->gpio_mask_clk);

            {
                // We change the data pin between the clock pin manipulations to widen the pulse slightly.
                uint32_t v = *ctx->txbuf & ctx->txmask;
                GPIO_REG_WRITE((v ? GPIO_OUT_W1TS_ADDRESS : GPIO_OUT_W1TC_ADDRESS), ctx->gpio_mask_data);
            }
            // Now we cause a positive edge.
            GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, ctx->gpio_mask_clk);

#if WS2811_BIT_ORDER == WS2811_MSBF
            ctx->txmask >>= 1;
            if (ctx->txmask)
                break;
#else
            ctx->txmask <<= 1;
            if (ctx->txmask == 1u << (uint32_t)WS2811_BITS_PER_PIXEL)
                break;
#endif

            // Next pixel.
            --ctx->txlen;
            if (ctx->txlen) {
                ++ctx->txbuf;
#if WS2811_BIT_ORDER == WS2811_MSBF
                ctx->txmask = 1u << (uint32_t)(WS2811_BITS_PER_PIXEL - 1);
#else
                ctx->txmask = 1;
#endif
                break;
            }

            // End of buffer. Wait for final bit to complete.
            ctx->state = WS2811_STATE_FINISH;
            break;

        case WS2811_STATE_FINISH:
            // Final bit done. Keep signal low for Tres.
            ctx->state = WS2811_STATE_RESET;
            GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, ctx->gpio_mask_all);
            RTC_REG_WRITE(FRC1_CTRL_ADDRESS, TIMER1_DIVIDE_BY_1 | TIMER1_ENABLE_TIMER);
            ws2811_timer_arm(ws2811_ns_to_rtc_timer_ticks(WS2811_TRES, 1));
            break;
        }

        case WS2811_STATE_IDLE: // Should not happen.
        case WS2811_STATE_RESET:
            // Ready for new transmission.
            ctx->state = WS2811_STATE_IDLE;
            TM1_EDGE_INT_DISABLE();
            RTC_REG_WRITE(FRC1_CTRL_ADDRESS, 0);
            break;
        }
    }
#undef ctx
}

/**
 * Add the given context to the interrupt handler contexts.
 * @return Zero on success. Non-zero if there is no space.
 */
static int ws2811_add_intr_ctx(struct ws2811_context *ctx) {
    for (int i = 0; i < WS2811_MAX_NUM_CONTEXTS; ++i) {
        if (!ws2811_intr_ctxs[i]) {
            ws2811_intr_ctxs[i] = ctx;
            return 0;
        }
    }

    return 1;
}

int ICACHE_FLASH_ATTR ws2811_init(struct ws2811_context *ctx, uint8_t gpio_clk, uint8_t gpio_data) {
    os_memset(ctx, 0, sizeof(*ctx));

    ctx->gpio_mask_clk = 1u << gpio_clk;
    ctx->gpio_mask_data = 1u << gpio_data;
    ctx->gpio_mask_all = ctx->gpio_mask_clk | ctx->gpio_mask_data;
    GPIO_OUTPUT_SET(gpio_clk, 0);
    GPIO_OUTPUT_SET(gpio_data, 0);

    if (!ws2811_intr_ctxs[0]) {
        // First context. Initialize system.
        RTC_REG_WRITE(FRC1_CTRL_ADDRESS, 0);
        ETS_FRC1_INTR_DISABLE();
        ETS_FRC_TIMER1_INTR_ATTACH(NULL, NULL);
        ETS_FRC_TIMER1_NMI_INTR_ATTACH(ws2811_tx_intr);
        RTC_CLR_REG_MASK(FRC1_INT_ADDRESS, FRC1_INT_CLR_MASK);
    }

    if (ws2811_add_intr_ctx(ctx))
        return 1;

    return 0;
}

void ICACHE_FLASH_ATTR ws2811_send(struct ws2811_context *ctx, const uint32_t *buf, size_t len) {
    if (ws2811_is_sending(ctx))
        return;

    if (!len)
        return;

    ctx->txbuf = buf;
    ctx->txlen = len;
#if WS2811_BIT_ORDER == WS2811_MSBF
    ctx->txmask = 1u << (uint32_t)(WS2811_BITS_PER_PIXEL - 1);
#else
    ctx->txmask = 1;
#endif
    ctx->state = WS2811_STATE_BIT;

    RTC_REG_WRITE(FRC1_CTRL_ADDRESS, TIMER1_DIVIDE_BY_1 | TIMER1_ENABLE_TIMER | TIMER1_AUTO_LOAD);
    ws2811_timer_arm(ws2811_ns_to_rtc_timer_ticks(WS2811_TBIT, 1));
    TM1_EDGE_INT_ENABLE();
}
