#include <ets_sys.h>
#include <gpio.h>
#include <osapi.h>
#include <user_interface.h>

#ifdef WS2811_IMPL_I2S
#include <pin_mux_register.h>
#include <ws2811-esp8266-i2s.h>
#define WS2811_CONTEXT struct ws2811_i2s_context
#define WS2811_INIT(ctx)                                                                                               \
    do {                                                                                                               \
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_I2SO_BCK);                                                         \
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_I2SO_DATA);                                                       \
        ws2811_i2s_init((ctx));                                                                                        \
    \
} while (0)
#define WS2811_SEND(ctx, buf, len) ws2811_i2s_send((ctx), (buf), (len))
#else
#include <ws2811-esp8266.h>
#define WS2811_CONTEXT struct ws2811_context
#define WS2811_INIT(ctx)                                                                                               \
    do {                                                                                                               \
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);                                                           \
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);                                                           \
        gpio_init();                                                                                                   \
        ws2811_init((ctx), 12, 13);                                                                                    \
    \
} while (0)
#define WS2811_SEND(ctx, buf, len) ws2811_send((ctx), (buf), (len))
#endif

/* --- Functions --- */
extern void ets_isr_unmask(uint32_t);
extern void ets_memset(void *, uint8_t, int);
extern void ets_printf(const char *, ...);
extern void ets_timer_arm_new(ETSTimer *, int, int, int);
extern void ets_timer_disarm(ETSTimer *);
extern void ets_timer_setfn(ETSTimer *, ETSTimerFunc, void *);
extern void uartAttach(void);
extern void uart_div_modify(int, int);
extern int UartGetCmdLn(char *buf);

/* --- Data --- */
// 0x00GGRRBB
static uint32_t led_buf[120];
static const uint8_t led_buf_size = sizeof(led_buf) / sizeof(*led_buf);
static WS2811_CONTEXT ws2811;
static os_timer_t send_tmr;

static void ICACHE_FLASH_ATTR send_timeout(void *arg) {
    static uint32_t i = 0;
    WS2811_CONTEXT *ctx = (WS2811_CONTEXT *)arg;

    uint32_t pos = i % led_buf_size;
    led_buf[pos] = 0x010101;
    ++i;
    pos = i % led_buf_size;
    led_buf[pos] = 0x7F7F7F;

    WS2811_SEND(ctx, led_buf, led_buf_size);
    ets_printf(".");

    char cmdline[128];
    if (!UartGetCmdLn(cmdline) && cmdline[0] == 'q') {
        ets_printf("%s", cmdline);
        system_restart();
    }
}

static void ICACHE_FLASH_ATTR inited(void) {
    os_timer_setfn(&send_tmr, send_timeout, &ws2811);
    os_timer_arm(&send_tmr, 20 /* ms */, 1 /* autoload */);

    ets_printf("booted\n");
}

void ICACHE_FLASH_ATTR user_init() {
    uartAttach();
    uart_div_modify(0, UART_CLK_FREQ / 115200);
    ETS_UART_INTR_ENABLE();

    WS2811_INIT(&ws2811);
    os_memset(led_buf, 0, sizeof(led_buf));

    system_init_done_cb(inited);
}
