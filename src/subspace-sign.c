#include <ets_sys.h>
#include <gpio.h>
#include <osapi.h>
#include <user_interface.h>

#include <ws2811-esp8266.h>

/* --- Functions --- */
extern void ets_memset(void *, uint8_t, int);
extern void ets_printf(const char *, ...);
extern void ets_timer_arm_new(ETSTimer *, int, int, int);
extern void ets_timer_disarm(ETSTimer *);
extern void ets_timer_setfn(ETSTimer *, ETSTimerFunc, void *);
extern void uart_div_modify(int, int);

/* --- Data --- */
// 0x00GGRRBB
static uint32_t led_buf[120];
static const uint8_t led_buf_size = sizeof(led_buf) / sizeof(*led_buf);
static struct ws2811_context ws2811;
static os_timer_t send_tmr;

static void ICACHE_FLASH_ATTR send_timeout(void *arg) {
    static uint32_t i = 0;
    struct ws2811_context *ctx = (struct ws2811_context *)arg;

    uint32_t pos = i % led_buf_size;
    if (pos) {
        led_buf[pos] = 0x010101;
    }
    ++i;
    pos = i % led_buf_size;
    if (pos) {
        led_buf[pos] = 0x7F7F7F;
    }

    ws2811_send(ctx, led_buf, led_buf_size);
    ets_printf(".");
}

void ICACHE_FLASH_ATTR user_init() {
    uart_div_modify(0, UART_CLK_FREQ / 115200);
    gpio_init();

    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
    ws2811_init(&ws2811, 12, 13);

    os_memset(led_buf, 0, sizeof(led_buf));
    led_buf[0] = 0x555555;
    os_timer_setfn(&send_tmr, send_timeout, &ws2811);
    os_timer_arm(&send_tmr, 20 /* ms */, 1 /* autoload */);

    ets_printf("booted\n");
}
