/**
 * WS2811/WS2812 driver using the I2S hardware.
 *
 * Using FIFO for simplicity over DMA. The buffers are deep enough that simple timing is not a problem. Unlike the SPI
 * subsystem, I2S has an interrupt that fires at a configurable low mark. SPI only has an "empty" which is too late.
 *
 * Inspired by
 *   https://github.com/CHERTS/esp8266-devkit/blob/ffbaaa3199ee603ee12f8c12826891c07bd8cc8a/Espressif/examples/ESP8266/I2S_Demo/driver/i2s.c
 *   https://github.com/espressif/esp-idf/blob/1f055d28b89cb4f3bd4be02eb1d7c75adbc08331/components/driver/i2s.c
 *   https://git.ccczh.ch/ari/nodemcu-firmware/blob/7ae293d5664dfef4cfdaaa641beec38dda9bd547/app/driver/spi.c
 *   https://github.com/espressif/ESP8266_MP3_DECODER/blob/7552a62d425598d64ebc255d32fa4f20220e92f6/mp3/driver/i2s_freertos.c
 */
#include "ws2811-esp8266-i2s.h"

#include <eagle_soc.h>
#include <ets_sys.h>
#include <osapi.h>

#include "i2s_register.h"
#include "spi_register.h"
#if WS2811_I2S_USE_DMA
#include "slc_register.h"
#endif

/* --- Macros --- */
#define WS2811_I2S_TRES 50000 // ns
#define WS2811_I2S_T0H 425    // ns
#define WS2811_I2S_TBIT (3 * WS2811_I2S_T0H)
// Assume T0H+T0L = T1H+T1L = TBIT.
// Assume T0H=425ns: T0L=850ns, T1H=850ns, T1L=425ns
//            +75ns      +50ns     +150ns     -175ns
// Then T1L is the only one out of spec for WS2812.
// Then T0H = div * 1/APB_CLK_FREQ => div = T0H * APB_CLK_FREQ = 68
// 68 = clkm * bck, where 0 < clk, bck < 64
#define WS2811_I2S_BCK 4
#define WS2811_I2S_CLKM 17

#define WS2811_SPI_INT_ST (PERIPHS_DPORT_BASEADDR | 0x20)
#define WS2811_SPI_INT_ST_I2S BIT9

/* --- Functions --- */
extern void ets_isr_attach(int, void (*)(void *), void *);
extern void ets_isr_mask(uint32_t);
extern void ets_isr_unmask(uint32_t);
extern void ets_memset(void *, uint8_t, int);
extern void ets_timer_arm_new(ETSTimer *, int, int, int);
extern void ets_timer_disarm(ETSTimer *);
extern void ets_timer_setfn(ETSTimer *, ETSTimerFunc, void *);
extern void rom_i2c_writeReg_Mask(uint8_t block, uint8_t host_id, uint32_t reg_add, uint8_t msb, uint8_t lsb,
                                  uint8_t indata);

/* --- Data --- */
static const uint16_t NIBBLE_PWM[] = {
#define PWM_BIT(b) ((b) ? 6 : 4)
#define PWM_WORD(v) (PWM_BIT((v)&8) << 9) | (PWM_BIT((v)&4) << 6) | (PWM_BIT((v)&2) << 3) | PWM_BIT((v)&1)
    PWM_WORD(0), PWM_WORD(1), PWM_WORD(2),  PWM_WORD(3),  PWM_WORD(4),  PWM_WORD(5),  PWM_WORD(6),  PWM_WORD(7),
    PWM_WORD(8), PWM_WORD(9), PWM_WORD(10), PWM_WORD(11), PWM_WORD(12), PWM_WORD(13), PWM_WORD(14), PWM_WORD(15),
#undef PWM_WORD
#undef PWM_BIT
};

static inline void bbpll_set_i2s_clock(bool b) { rom_i2c_writeReg_Mask(0x67, 4, 4, 7, 7, b ? 1 : 0); }

// Must be in IRAM, used by ISR.
static void ws2811_i2s_fill(struct ws2811_i2s_context *ctx) {
    while (!(READ_PERI_REG(I2SINT_RAW) & I2S_I2S_TX_WFULL_INT_RAW) && ctx->txlen) {
        // Fill one byte, which becomes 24 bits. Place in MSB.
        WRITE_PERI_REG(I2STXFIFO, ((uint32_t)NIBBLE_PWM[(*ctx->txbuf >> (ctx->txbit + 4)) & 0xF] << (32 - 12)) |
                                      ((uint32_t)NIBBLE_PWM[(*ctx->txbuf >> ctx->txbit) & 0xF]) << (32 - 24));
        ctx->txbit += 8;
        if (ctx->txbit == WS2811_I2S_BITS_PER_PIXEL) {
            ctx->txbit = 0;
            ++ctx->txbuf;
            --ctx->txlen;
        }
    }

    if (ctx->txlen)
        return;

    while (!(READ_PERI_REG(I2SINT_RAW) & I2S_I2S_TX_WFULL_INT_RAW) && ctx->trailer_len) {
        WRITE_PERI_REG(I2STXFIFO, 0);
        --ctx->trailer_len;
    }

    if (ctx->trailer_len)
        return;

    CLEAR_PERI_REG_MASK(I2SINT_ENA, I2S_I2S_TX_PUT_DATA_INT_ENA);
    SET_PERI_REG_MASK(I2SINT_ENA, I2S_I2S_TX_REMPTY_INT_ENA);
    ctx->state = WS2811_I2S_STATE_RESET;
}

#define GPIO2_TOGGLE GPIO_OUTPUT_SET(2, (gpio2 = ~gpio2) & 1)
static void ws2811_i2s_intr(void *cookie) {
    uint32_t int_st = READ_PERI_REG(WS2811_SPI_INT_ST);

    if (int_st & BIT4) {
        CLEAR_PERI_REG_MASK(SPI_SLAVE(0), 0x3FF);
    }
    if (int_st & BIT7) {
        CLEAR_PERI_REG_MASK(SPI_SLAVE(1), 0x3FF);
    }
    if (int_st & WS2811_SPI_INT_ST_I2S) {
        struct ws2811_i2s_context *ctx = (struct ws2811_i2s_context *)cookie;
        switch (ctx->state) {
        case WS2811_I2S_STATE_SENDING:
            ws2811_i2s_fill(ctx);
            break;

        case WS2811_I2S_STATE_RESET:
            CLEAR_PERI_REG_MASK(I2SINT_ENA, I2S_I2S_TX_REMPTY_INT_ENA);
            CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_START);
            ctx->state = WS2811_I2S_STATE_IDLE;
            break;
        }

        WRITE_PERI_REG(I2SINT_CLR, 0xFFFFFFFF);
        WRITE_PERI_REG(I2SINT_CLR, 0);
    }
}

int ICACHE_FLASH_ATTR ws2811_i2s_init(struct ws2811_i2s_context *ctx) {
    os_memset(ctx, 0, sizeof(*ctx));

    ETS_SPI_INTR_DISABLE();
    ETS_SPI_INTR_ATTACH(ws2811_i2s_intr, ctx);
    CLEAR_PERI_REG_MASK(SPI_SLAVE(0), 0x3FF);
    CLEAR_PERI_REG_MASK(SPI_SLAVE(1), 0x3FF);
    WRITE_PERI_REG(I2SINT_ENA, 0);
    ETS_SPI_INTR_ENABLE();

    WRITE_PERI_REG(I2SCONF, I2S_I2S_RESET_MASK);
    WRITE_PERI_REG(I2SCONF,
                   (WS2811_I2S_BCK << I2S_BCK_DIV_NUM_S) | (WS2811_I2S_CLKM << I2S_CLKM_DIV_NUM_S) |
                       (8 << I2S_BITS_MOD_S)); // 16+I2S_BITS_MOD

#define FIFO_MODE (WS2811_I2S_TX_FIFO_MOD_24BIT_DISCONT_DUAL << I2S_I2S_TX_FIFO_MOD_S) | (32 << I2S_I2S_TX_DATA_NUM_S)
    WRITE_PERI_REG(I2S_FIFO_CONF, FIFO_MODE);
    WRITE_PERI_REG(I2SCONF_CHAN, (WS2811_I2S_TX_CHAN_DUAL << I2S_TX_CHAN_MOD_S));

    bbpll_set_i2s_clock(true);

    return 0;
}

void ICACHE_FLASH_ATTR ws2811_i2s_send(struct ws2811_i2s_context *ctx, const uint32_t *buf, size_t len) {
    if (ws2811_i2s_is_sending(ctx))
        return;

    if (!len)
        return;

    ctx->txbuf = buf;
    ctx->txlen = len;
    ctx->txbit = 0;
#define FRAME_TIME (2 * 8 * WS2811_I2S_TBIT)
#define REMPTY_DELAY_FRAMES (2 - 1)
    // The precision of os_timer_arm_us is 500 µs, which is much higher than TBIT.
    // We continue filling the FIFO with zero instead, giving us better precision.
    // We need to write at least one frame to ensure the last data byte isn't repeating.
    // The REMPTY interrupt comes two frames early, but since the hardware starts each transfer with one zero frame,
    // we can discount one.
    // The trailer length is in samples.
    ctx->trailer_len = 2 * ((WS2811_I2S_TRES + FRAME_TIME - 1) / FRAME_TIME + REMPTY_DELAY_FRAMES);
#undef FRAME_TIME
    // The total output must be even since we have two channels.
    if ((WS2811_I2S_BITS_PER_PIXEL / 8 * ctx->txlen + ctx->trailer_len) & 1)
        ++ctx->trailer_len;

    ctx->state = WS2811_I2S_STATE_SENDING;

    ETS_SPI_INTR_DISABLE();
    CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_START);
    SET_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_RESET | I2S_I2S_TX_FIFO_RESET);
    CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_RESET | I2S_I2S_TX_FIFO_RESET);
    WRITE_PERI_REG(I2SINT_ENA, I2S_I2S_TX_PUT_DATA_INT_ENA);
    ws2811_i2s_fill(ctx);
    WRITE_PERI_REG(I2SINT_CLR, 0xFFFFFFFF);
    WRITE_PERI_REG(I2SINT_CLR, 0);
    SET_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_START);
    ETS_SPI_INTR_ENABLE();
}
