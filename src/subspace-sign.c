#include <ets_sys.h>
#include <gpio.h>
#include <osapi.h>
#include <stdlib.h>
#include <time.h>
#include <user_interface.h>

#include <firebase-http.h>
#include <lwip-esp8266.h>
#include "clock.h"

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
extern void ets_memcpy(void *, const void *, int);
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
static const uint8_t LED_BUF_SIZE = sizeof(led_buf) / sizeof(*led_buf);
static WS2811_CONTEXT ws2811;
static os_timer_t send_tmr;
static void (*update_leds)(void);
static os_timer_t mode_tmr;
static struct clock_context clockctx;

static inline void ICACHE_FLASH_ATTR update_running_light(void) {
    static uint32_t i = 0;
    uint32_t pos = i % LED_BUF_SIZE;
    led_buf[pos] = 0;
    ++i;
    pos = i % LED_BUF_SIZE;
    led_buf[pos] = 0x7F7F7F;
}

static inline void ICACHE_FLASH_ATTR update_clock(void) { clock_update(&clockctx); }

static void ICACHE_FLASH_ATTR send_timeout(void *arg) {
    WS2811_CONTEXT *ctx = (WS2811_CONTEXT *)arg;

    update_leds();
    WS2811_SEND(ctx, led_buf, LED_BUF_SIZE);

    char cmdline[128];
    if (!UartGetCmdLn(cmdline) && cmdline[0] == 'q') {
        ets_printf("%s", cmdline);
        system_restart();
    }
}

static void ICACHE_FLASH_ATTR mode_timeout(void *arg) {
    if (update_leds == update_running_light && clock_is_valid(&clockctx)) {
        update_leds = update_clock;
    }
}
static void ICACHE_FLASH_ATTR scan_done(void *arg, STATUS status) {
    if (status != OK) {
        ets_printf("scan failed: %d\n", status);
        return;
    }

    struct bss_info *best = NULL;
    for (struct bss_info *bssp = (struct bss_info *)arg; bssp; bssp = STAILQ_NEXT(bssp, next)) {
        ets_printf("  scan %d %s\n", bssp->authmode, bssp->ssid);
        if (bssp->authmode != AUTH_OPEN) {
            continue;
        }
        if (!best || best->rssi < bssp->rssi) {
            best = bssp;
        }
    }

    if (best) {
        struct station_config stacfg;
        os_memcpy(stacfg.ssid, best->ssid, sizeof(stacfg.ssid));
        os_memcpy(stacfg.bssid, best->bssid, sizeof(stacfg.bssid));
        stacfg.bssid_set = 1;
        wifi_station_set_config(&stacfg);
        wifi_station_connect();
    }
}

static void ICACHE_FLASH_ATTR start_wifi_scan(void) {
    struct scan_config scancfg;
    os_memset(&scancfg, 0, sizeof(scancfg));
    wifi_station_scan(&scancfg, scan_done);
}

static void ICACHE_FLASH_ATTR inited(void) {
    os_timer_setfn(&send_tmr, send_timeout, &ws2811);
    // If TxH+TxL = 1.2 µs, then 120 LEDs take 1.2 * 24 * 120 = 3.5 ms.
    // So that's a minimum bound.
    os_timer_arm(&send_tmr, 20 /* ms */, 1 /* autoload */);

    os_timer_setfn(&mode_tmr, mode_timeout, NULL);
    os_timer_arm(&mode_tmr, 1000 /* ms */, 1 /* autoload */);

    ets_printf("booted\n");
    start_wifi_scan();
}

static void ICACHE_FLASH_ATTR handle_wifi_event(System_Event_t *event) {
    switch (event->event) {
    case EVENT_STAMODE_GOT_IP:
        ets_printf("Connected\n");
        break;

    case EVENT_STAMODE_DISCONNECTED:
        ets_printf("Disconnected\n");
        break;
    }
}

void ICACHE_FLASH_ATTR user_init() {
    uartAttach();
    uart_div_modify(0, UART_CLK_FREQ / 115200);
    ETS_UART_INTR_ENABLE();

    wifi_station_set_auto_connect(false);
    wifi_set_opmode(STATION_MODE);
    wifi_set_event_handler_cb(handle_wifi_event);

    lwip_esp8266_init();
    
    WS2811_INIT(&ws2811);
    os_memset(led_buf, 0, sizeof(led_buf));
    update_leds = update_running_light;

    if (!clock_init(&clockctx, led_buf, LED_BUF_SIZE)) {
        ets_printf("Failed clock_init\n");
        return;
    }

    system_init_done_cb(inited);
}
