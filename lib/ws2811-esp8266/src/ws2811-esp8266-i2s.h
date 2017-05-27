#ifndef WS2811_ESP8266_I2S_H_
#define WS2811_ESP8266_I2S_H_

#include <user_interface.h>

/* --- Macros --- */
#ifndef WS2811_I2S_MAX_NUM_CONTEXTS
/**
 * The maximum number of contexts we support. Each context wastes four bytes and ISR overhead.
 */
#define WS2811_I2S_MAX_NUM_CONTEXTS 1
#endif
#ifndef WS2811_I2S_BITS_PER_PIXEL
/**
 * Number of bits per LED unit. Normally 24 (8-bit RGB).
 * Must be a multiple of eight.
 */
#define WS2811_I2S_BITS_PER_PIXEL 24
#endif
#define WS2811_I2S_MSBF 1
#define WS2811_I2S_LSBF 2
#ifndef WS2811_I2S_BIT_ORDER
/**
 * Ordering of bits on the wire. Normally MSB first.
 */
#define WS2811_I2S_BIT_ORDER WS2811_I2S_MSBF
#endif

/* --- Types --- */
typedef enum {
    WS2811_I2S_STATE_IDLE,
    WS2811_I2S_STATE_SENDING,
    WS2811_I2S_STATE_TRAILER,
    WS2811_I2S_STATE_FINISH,
    WS2811_I2S_STATE_RESET,
} ws2811_i2s_state;

struct ws2811_i2s_context {
    ws2811_i2s_state state;
    const uint32_t *txbuf;
    int txlen;
    int txbit;
    int trailer_len; // Number of samples
};

/* --- Functions --- */
/**
 * Initialize a new WS2811 LED bus context.
 *
 * This sets up the GPIO pins and the interrupt routines.
 *
 * @param ctx The context to be initialized.
 * @return Zero on success and non-zero if no more contexts can be created. See WS2811_MAX_NUM_CONTEXTS.
 */
extern int ICACHE_FLASH_ATTR ws2811_i2s_init(struct ws2811_i2s_context *ctx);

/**
 * Send a buffer of pixel data.
 *
 * If the context is already sending data, this function does nothing.
 *
 * @param ctx The context of the bus to send to.
 * @param buf The pixel buffer to send. Only the lower WS2811_BITS_PER_PIXEL bits are sent.
 * @param len The length of buf, in pixels.
 */
extern void ICACHE_FLASH_ATTR ws2811_i2s_send(struct ws2811_i2s_context *ctx, const uint32_t *buf, size_t len);

/**
 * Return whether the context is currently sending data.
 */
static inline bool ws2811_i2s_is_sending(struct ws2811_i2s_context *ctx) { return ctx->state != WS2811_I2S_STATE_IDLE; }

#endif /* WS2811_ESP8266_I2S_H_ */
