#include "firebase-http.h"

#include <lwip/dns.h>
#include <lwip/tcp.h>

int ICACHE_FLASH_ATTR fb_http_request_init(struct FbHttpRequest *req) {
	os_memset(req, 0, sizeof(*req));
}

void ICACHE_FLASH_ATTR fb_http_request_clean(struct FbHttpRequest *req) {
	if (req->conn_state != FB_HTTP_REQUEST_CONN_IDLE) {
		req->conn_state = FB_HTTP_REQUEST_CONN_IDLE;
		// disconnect
	}

	req->rx_state = FB_HTTP_REQUEST_RX_IDLE;
	req->tx_state = FB_HTTP_REQUEST_TX_IDLE;
}

struct FbUrlParts {
	int schema;
	int schema_len;
	int host;
	int host_len;
	int path;
};

static int ICACHE_FLASH_ATTR parse_url(struct FbHttpRequest *req, const char *url, struct FbUrlParts *out) {
	size_t len = os_strlen(url);

	// Largest schema plus one character.
	if (len < 9) return 1;

	out->schema = 0;

	size_t i = 0;

	if (!os_memcmp(url, "https://")) {
		i += sizeof("https://") - 1;
		out->schema_len = i - 3 - out->schema;
	} else {
		return 1; // Unsupported schema.
	}

	out->host = i;

	const char *slash = os_strchr(&url[i], '/');
	if (!slash) {
		slash = os_strchr(&url[i], '?');
		if (!slash) slash = url + len;
	}

	const char *at = os_strchr(&url[i], '@');
	if (at && at < slash) return 2;  // Authentication not supported.

	if (url[i] == '[') return 3;  // IPv6 host literal not supported.

	const char *colon = os_strchr(&url[i], ':');
	if (colon && colon < slash) return 4;  // Explicit port not supported.

	i = slash - url;
	out->host_len = i - out->host;
	out->path = i;

	return 0;
}

static void ICACHE_FLASH_ATTR host_connected(void *arg, struct pcb *tpcb, err_t err) {
	// ...
}

static void ICACHE_FLASH_ATTR host_resolved(const char *name, struct ip_addr *ipaddr, void *arg) {
	struct FbHttpRequest *req = (struct FbHttpRequest*)arg;

	req->conn_state = FB_HTTP_REQUEST_CONN_CONNECTING;
	req->tpcb = tcp_new();
	if (!req->tpcb) {
		req->func(req, FB_HTTP_REQUEST_FAILED);
		req->conn_state = FB_HTTP_REQUEST_CONN_IDLE;
		return;
	}
	tcp_arg(req->tpcb, req);

	err_t err = tcp_connect(req->tpcb, ipaddr, 443, host_connected);
	if (err == ERR_OK) {
		host_connected(req, req->tpcb, ERR_OK);
	} else if (err != ERR_INPROGRESS) {
		req->func(req, FB_HTTP_REQUEST_FAILED);
		req->conn_state = FB_HTTP_REQUEST_CONN_IDLE;
	}

	#if 0
	req->tx_state = FB_HTTP_REQUEST_TX_METHOD;

	if (espconn_secure_send(&req->conn, method, os_strlen(method)))
		return -2;

	if (espconn_secure_send(&req->conn, " ", 1))
		return -2;

	req->tx_state = FB_HTTP_REQUEST_TX_URL;

	if (espconn_secure_send(&req->conn, &url[url_parts.path], os_strlen(&url[url_parts.path])))
		return -2;

	req->tx_state = FB_HTTP_REQUEST_TX_VERSION;

	static const char SPACE_HTTP_VERSION_EOL[] = " HTTP/1.1\r\n";
	if (espconn_secure_send(&req->conn, SPACE_HTTP_VERSION_EOL, sizeof(SPACE_HTTP_VERSION_EOL) - 1))
		return -2;

	req->tx_state = FB_HTTP_REQUEST_TX_HEADER_KEY;
	#endif
}

static int ICACHE_FLASH_ATTR do_resolve(struct FbHttpRequest *req, const char *hostname) {
	req->conn_state = FB_HTTP_REQUEST_CONN_RESOLVING;

	ip_addr_t ipaddr;
	err_t err = dns_gethostbyname(req->txbuf, &ipaddr, host_resolved, req);
	if (err == ERR_OK) {
		host_resolved(req->txbuf, &ipaddr, req);
	} else if (err != ERR_INPROGRESS) {
		return -3;
	}

	return 0;
}

int ICACHE_FLASH_ATTR fb_http_request_write_request_url(struct FbHttpRequest *req, const char *method, const char *url) {
	struct FbUrlParts url_parts;

	if (parse_url(req, url, &url_parts)) return -1;

	if (req->conn_state == FB_HTTP_REQUEST_CONN_IDLE) {
		req->txbuf = os_malloc(url_parts.host_len + 1);
		if (!req->txbuf) return -2;

		os_memcpy(req->txbuf, &url[url_parts.host], url_parts.host_len);
		req->txbuf[url_parts.host_len] = '\0';

		return do_resolve(req, req->txbuf);
	}
}

int ICACHE_FLASH_ATTR fb_http_request_read_status(struct FbHttpRequest *req, char *msgbuf, fb_ssize_t msgsize) {

}
