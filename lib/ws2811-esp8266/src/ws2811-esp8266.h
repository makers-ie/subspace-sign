#ifndef WS2811_ESP8266_H_
#define WS2811_ESP8266_H_

#include <user_interface.h>

/* --- Macros --- */
#ifndef WS2811_MAX_NUM_CONTEXTS
/**
 * The maximum number of contexts we support. Each context wastes four bytes and ISR overhead.
 */
#define WS2811_MAX_NUM_CONTEXTS 1
#endif
#ifndef WS2811_BITS_PER_PIXEL
/**
 * Number of bits per LED unit. Normally 24 (8-bit RGB).
 */
#define WS2811_BITS_PER_PIXEL 24
#endif
#define WS2811_MSBF 1
#define WS2811_LSBF 2
#ifndef WS2811_BIT_ORDER
/**
 * Ordering of bits on the wire. Normally MSB first.
 */
#define WS2811_BIT_ORDER WS2811_MSBF
#endif

/* --- Types --- */
typedef enum {
    WS2811_STATE_IDLE,
    WS2811_STATE_BIT,
    WS2811_STATE_FINISH,
    WS2811_STATE_RESET,
} ws2811_state;

struct ws2811_context {
    ws2811_state state;
    uint32_t gpio_mask_clk;
    uint32_t gpio_mask_data;
    uint32_t gpio_mask_all;
    const uint32_t *txbuf;
    int txlen;
    uint32_t txmask;
};

/* --- Functions --- */
/**
 * Initialize a new WS2811 LED bus context.
 *
 * This sets up the GPIO pins and the interrupt routines.
 *
 * The ESP8266 is not fast enough to service the WS2811 protocol purely from timer interrupts, so we leave it to
 * hardware to do the PWM. We use two outputs, and they are essentially an SPI bus, but instead of leaving the clock
 * high for half a period, it only sees a pulse (meaning we get away with half the timer frequency). The data pin is
 * set to high or low before the pulse.
 *
 * To convert the data to WS2811 signals, the clock is attached to two fast monostable multivibrators, like 74HC123. One
 * is daisy-chained with the other to cause the last one to retrigger when the first one resets the pulse. The data
 * signal simply connects to the nCLR pin of the first multivibrator, causing it to either pulse or not pulse. If the
 * retrigger pulse comes slightly before the second multivibrator resets, it will continue its pulse for longer, thus
 * outputting a high bit. This depends on fairly precise timing in the multivibrators, but means we don't have to make
 * the ESP8266 go faster than the bit interval. (Another approach would have been to use one multivibrator for low and
 * the other for high, with different pulse settings. This would have required an additional OR-gate, or relying on an
 * open-drain signal at the output.)
 *
 * Because this uses NMI and there is a bug in the ESP8266 SDK, the following is needed in the linker script,
 * at the start of the .data section:
 *
 *   _Pri_3_HandlerAddress = ABSOLUTE(.);
 *   . = ABSOLUTE(4);
 *
 * Each GPIO will be set to outputs, but the pin muxes must be set up already.
 *
 * @param ctx The context to be initialized.
 * @param gpio_no_clk The pin that will be used to send a high pulse at the start of every cycle.
 * @param gpio_no_data The pin that will be high or low depending on the bit to send.
 * @return Zero on success and non-zero if no more contexts can be created. See WS2811_MAX_NUM_CONTEXTS.
 */
extern int ICACHE_FLASH_ATTR ws2811_init(struct ws2811_context *ctx, uint8_t gpio_no_clk, uint8_t gpio_no_data);

/**
 * Send a buffer of pixel data.
 *
 * If the context is already sending data, this function does nothing.
 *
 * @param ctx The context of the bus to send to.
 * @param buf The pixel buffer to send. Only the lower WS2811_BITS_PER_PIXEL bits are sent.
 * @param len The length of buf, in pixels.
 */
extern void ICACHE_FLASH_ATTR ws2811_send(struct ws2811_context *ctx, const uint32_t *buf, size_t len);

/**
 * Return whether the context is currently sending data.
 */
static inline bool ws2811_is_sending(struct ws2811_context *ctx) { return ctx->state != WS2811_STATE_IDLE; }

#endif /* WS2811_ESP8266_H_ */
