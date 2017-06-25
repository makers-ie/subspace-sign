#include "lwip-esp8266.h"

#include <lwip/init.h>
#include <lwip/tcp_impl.h>
#include <lwip/timers.h>

/* --- Data --- */
static os_timer_t tcp_timer;

static void ICACHE_FLASH_ATTR tcp_timeout(void *arg) {
	sys_check_timeouts();
}

extern void ICACHE_FLASH_ATTR lwip_esp8266_init(void) {
	lwip_init();

	os_timer_setfn(&tcp_timer, tcp_timeout, NULL);
	os_timer_arm(&tcp_timer, TCP_TMR_INTERVAL, 1 /* autoload */);
}

extern void ICACHE_FLASH_ATTR lwip_esp8266_clean(void) {
	os_timer_disarm(&tcp_timer);
}
