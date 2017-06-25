#ifndef FIREBASE_ESP8266_HTTP_H_
#define FIREBASE_ESP8266_HTTP_H_

#include <lwip/tcp.h>

#include "firebase-common.h"

/* --- Types --- */
struct FbHttpRequest;

typedef enum {
	FB_HTTP_REQUEST_CONN_IDLE,
	FB_HTTP_REQUEST_CONN_RESOLVING,
	FB_HTTP_REQUEST_CONN_CONNECTING,
	FB_HTTP_REQUEST_CONN_CONNECTED,
} FbHttpRequestConnState;

typedef enum {
	FB_HTTP_REQUEST_TX_IDLE,
	FB_HTTP_REQUEST_TX_METHOD,
	FB_HTTP_REQUEST_TX_URL,
	FB_HTTP_REQUEST_TX_VERSION,
	FB_HTTP_REQUEST_TX_HEADER_KEY,
	FB_HTTP_REQUEST_TX_HEADER_VALUE,
	FB_HTTP_REQUEST_TX_HEADER_END,
	FB_HTTP_REQUEST_TX_BODY,
	FB_HTTP_REQUEST_TX_ERROR,
} FbHttpRequestTxState;

typedef enum {
	FB_HTTP_REQUEST_RX_IDLE,
	FB_HTTP_REQUEST_RX_VERSION,
	FB_HTTP_REQUEST_RX_CODE,
	FB_HTTP_REQUEST_RX_MESSAGE,
	FB_HTTP_REQUEST_RX_HEADER_KEY,
	FB_HTTP_REQUEST_RX_HEADER_VALUE,
	FB_HTTP_REQUEST_RX_BODY,
	FB_HTTP_REQUEST_RX_ERROR,
} FbHttpRequestRxState;

typedef enum {
	FB_HTTP_REQUEST_SENT_REQUEST,
	FB_HTTP_REQUEST_SENT_HEADER,
	FB_HTTP_REQUEST_SENT_BODY_DATA,
	FB_HTTP_REQUEST_RECVD_STATUS,
	FB_HTTP_REQUEST_RECVD_HEADER,
	FB_HTTP_REQUEST_RECVD_BODY_DATA,
	FB_HTTP_REQUEST_FINISHED,
	FB_HTTP_REQUEST_FAILED,
} FbHttpRequestEvent;

typedef void (*FbHttpRequestFunc)(struct FbHttpRequest *req, FbHttpRequestEvent event);

struct FbHttpRequest {
	FbHttpRequestFunc func;
	FbHttpRequestConnState conn_state;
	FbHttpRequestRxState tx_state;
	FbHttpRequestTxState rx_state;

	char *txbuf;
	size_t txlen;

	uint8_t *rxbuf;
	size_t rxsize;
	size_t rxlen;

	struct tcp_pcb *tpcb;
};

/* --- Functions --- */
extern int fb_http_request_init(struct FbHttpRequest *req) ICACHE_FLASH_ATTR;
extern void fb_http_request_clean(struct FbHttpRequest *req) ICACHE_FLASH_ATTR;
extern int fb_http_request_write_request_url(struct FbHttpRequest *req, const char *method, const char *url) ICACHE_FLASH_ATTR;
extern int fb_http_request_write_header(struct FbHttpRequest *req, const char *key, const char *value) ICACHE_FLASH_ATTR;
extern fb_ssize_t fb_http_request_write(struct FbHttpRequest *req, const uint8_t *body, fb_ssize_t len) ICACHE_FLASH_ATTR;
extern int fb_http_request_read_status(struct FbHttpRequest *req, char *msgbuf, fb_ssize_t msgsize) ICACHE_FLASH_ATTR;
extern int fb_http_request_read_header(struct FbHttpRequest *req, char *keybuf, fb_ssize_t keysize, char *valuebuf, fb_ssize_t valuesize) ICACHE_FLASH_ATTR;
extern fb_ssize_t fb_http_request_read(struct FbHttpRequest *req, uint8_t *buf, fb_ssize_t size) ICACHE_FLASH_ATTR;

#endif /* FIREBASE_ESP8266_HTTP_H_ */
