/**
 * A simple LED clock module using SNTP for time.
 */
#include <osapi.h>
#include <sntp.h>
#include <time.h>

#include "clock.h"

/* --- Functions --- */
extern void ets_memset(void *, uint8_t, int);
extern void ets_printf(const char *, ...);

/* --- Data --- */
static const int MDAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

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
}
