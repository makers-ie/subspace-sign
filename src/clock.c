/**
 * A simple LED clock module using SNTP for time.
 */
#include <osapi.h>
#include <sntp.h>
#include <time.h>

#include "clock.h"

/* --- Types --- */
struct sparkle_sprite {
    uint32_t *led_buf;
    uint8_t led_buf_size;

    uint32_t color;
    uint8_t index;
    uint8_t att;
    int32_t delay; // µs per pixel
};

/* --- Functions --- */
extern void ets_memset(void *, uint8_t, int);
extern void ets_printf(const char *, ...);

/* --- Data --- */
static const int MDAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static void ICACHE_FLASH_ATTR sparkle_sprite_init(struct sparkle_sprite *sp, uint32_t *buf, uint8_t size,
                                                  uint32_t color, int8_t index) {
    sp->led_buf = buf;
    sp->led_buf_size = size;

    sp->color = color;
    sp->att = 1;
    if (index < 0) {
        sp->index = -index;
        sp->delay = -100000;
    } else {
        sp->index = index;
        sp->delay = 100000;
    }
}

static void ICACHE_FLASH_ATTR sparkle_sprite_draw(struct sparkle_sprite *sp) { sp->led_buf[sp->index] = sp->color; }

static bool ICACHE_FLASH_ATTR sparkle_sprite_update(struct sparkle_sprite *sp, int32_t dt_us) {
    static const uint32_t ATT_MASKS[] = {
        0xFFFFFFFF, 0x7F7F7F7F, 0x3F3F3F3F, 0x1F1F1F1F, 0x0F0F0F0F, 0x07070707, 0x03030303, 0x01010101,
    };

    int32_t abs_delay = (sp->delay < 0 ? -sp->delay : sp->delay);
    // TODO(tommie): Make this work with unaligned delays.
    int32_t nt = (dt_us + abs_delay / 2) / sp->delay;
    sp->index = (sp->index + nt + sp->led_buf_size) % sp->led_buf_size;

    uint8_t n = (nt < 0 ? -nt : nt);
    if (n * sp->att >= 8) {
        sp->color = 0;
    } else {
        sp->color >>= n * sp->att;
        sp->color &= ATT_MASKS[n * sp->att];
    }
    return sp->color != 0;
}

static int ICACHE_FLASH_ATTR cmp_last_wday_of_month(const struct tm *tm, int wday) {
    int mdays = MDAYS[tm->tm_mon];
    if (tm->tm_mon == 1) {
        // Leap year for February.
        int y = 1900 + tm->tm_year;
        mdays += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0 ? 1 : 0);
    }
    // TODO(tommie): Test this.
    int prev_day = tm->tm_mday - (tm->tm_wday + 7 - wday) % 7;
    return prev_day + 7 - mdays;
}

static bool ICACHE_FLASH_ATTR is_after_last_wday_hour_of_month(const struct tm *tm, int mon, int wday, int hour) {
    if (tm->tm_mon != mon) {
        return tm->tm_mon > mon;
    }
    int cmpmday = cmp_last_wday_of_month(tm, wday);
    if (cmpmday) {
        return cmpmday > 0;
    }
    return tm->tm_hour >= hour;
}

static void ICACHE_FLASH_ATTR mylocaltime_r(const time_t *t, struct tm *tm) {
    gmtime_r(t, tm);
    // Ireland DST/TZ rules.
    tm->tm_isdst = is_after_last_wday_hour_of_month(tm, 2, 0, 1) && !is_after_last_wday_hour_of_month(tm, 9, 0, 0);
    tm->tm_hour += 0 + (tm->tm_isdst ? 1 : 0);
    mktime(tm);
}

bool ICACHE_FLASH_ATTR clock_init(struct clock_context *ctx, uint32_t *led_buf, uint8_t led_buf_size) {
    if (led_buf_size != 120) {
        return false;
    }

    os_memset(ctx, 0, sizeof(*ctx));
    ctx->led_buf = led_buf;

    sntp_setservername(0, (char *)"2.pool.ntp.org");
    sntp_setservername(1, (char *)"3.pool.ntp.org");
    sntp_setservername(2, (char *)"0.pool.ntp.org");
    sntp_set_timezone(0);
    sntp_init();
}

bool ICACHE_FLASH_ATTR clock_is_valid(struct clock_context *ctx) { return sntp_get_current_timestamp() > 0; }

void ICACHE_FLASH_ATTR clock_update(struct clock_context *ctx) {
    time_t t = sntp_get_current_timestamp();
    if (!t) {
        return;
    }

    struct tm tm;
    mylocaltime_r(&t, &tm);
#if 0
    ets_printf("T %02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif

    os_memset(ctx->led_buf, 0, 120 * sizeof(*ctx->led_buf));
    uint8_t ih = (tm.tm_hour % 12) * 120 / 12;
    uint8_t im = tm.tm_min * 120 / 60;
    uint8_t is = tm.tm_sec * 120 / 60;

    ctx->led_buf[(ih - 2 + 120) % 120] |= 0x070000;
    ctx->led_buf[(ih - 1 + 120) % 120] |= 0x1F0000;
    ctx->led_buf[ih] |= 0x3F0000;
    ctx->led_buf[(ih + 1) % 120] |= 0x1F0000;
    ctx->led_buf[(ih + 2) % 120] |= 0x070000;

    ctx->led_buf[(im - 1 + 120) % 120] |= 0x001F00;
    ctx->led_buf[im] |= 0x007F00;
    ctx->led_buf[(im + 1) % 120] |= 0x001F00;

    ctx->led_buf[(is - 1 + 120) % 120] |= 0x00000F;
    ctx->led_buf[is] |= 0x00003F;
    ctx->led_buf[(is + 1) % 120] |= 0x00000F;

    static struct sparkle_sprite minute_sparkle[2];
    static bool minute_sparkle_alive[2];
    if (tm.tm_min != ctx->prev_tm.tm_min) {
        sparkle_sprite_init(&minute_sparkle[0], ctx->led_buf, 120, 0x7F7F00, im);
        sparkle_sprite_init(&minute_sparkle[1], ctx->led_buf, 120, 0x7F7F00, -im);
        minute_sparkle_alive[0] = true;
        minute_sparkle_alive[1] = true;
    }
    for (uint8_t i = 0; i < 2; ++i) {
        if (minute_sparkle_alive[i]) {
            minute_sparkle_alive[i] = sparkle_sprite_update(&minute_sparkle[i], 50000);
        }
        if (minute_sparkle_alive[i]) {
            sparkle_sprite_draw(&minute_sparkle[i]);
        }
    }

    ctx->prev_tm = tm;
}
