/* Byte transports carrying MAVLink: serial port, UDP (listen / send-to), TCP. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "mav.h"

typedef enum {
	LINK_NONE = 0,
	LINK_SERIAL,
	LINK_UDP_IN,  /* bind locally, reply to whoever talks to us (QGC style) */
	LINK_UDP_OUT, /* send to a fixed host:port */
	LINK_TCP,	  /* TCP client, e.g. ArduPilot SITL on 5760 */
} link_kind_t;

typedef struct link_spec {
	link_kind_t kind;
	char host[256]; /* serial device path or host name / address */
	int port;
	int baud;
} link_spec_t;

#define LINK_TX_SIZE 32768

typedef struct link {
	link_spec_t spec;
	int fd;
	bool connecting; /* TCP connect() in progress */
	bool peer_known; /* UDP_IN: have we heard from somebody? */
	struct sockaddr_storage peer;
	socklen_t peer_len;

	uint8_t tx[LINK_TX_SIZE];
	size_t tx_len;

	mavlink_message_t rx_msg; /* parser scratch */
	mavlink_status_t rx_status;
	mavlink_status_t tx_status; /* sequence numbers + v1/v2 framing flag */

	uint64_t rx_bytes, tx_bytes, rx_msgs, rx_crc_err, tx_dropped;
	char name[300];
	char err[200];
} link_t;

/* Parse a connection string:
 *   /dev/ttyACM0            serial (baud = default_baud)
 *   serial:/dev/ttyUSB0:57600
 *   /dev/ttyUSB0:57600
 *   udp:14550  udp::14550  udp:0.0.0.0:14550 udpin:...   listen
 *   udpout:192.168.4.1:14550                              send to
 *   tcp:127.0.0.1:5760  tcp:5760                          connect
 * Returns false and fills err on bad input. */
bool link_spec_parse(const char *uri, int default_baud, link_spec_t *out, char *err, size_t errlen);
void link_spec_describe(const link_spec_t *s, char *out, size_t n);

void link_init(link_t *l);
/* Opens the link. Returns false with l->err set on failure. */
bool link_open(link_t *l, const link_spec_t *spec);
void link_close(link_t *l);
static inline bool link_is_open(const link_t *l) { return l->fd >= 0; }

/* poll() events wanted for this link. */
short link_poll_events(const link_t *l);

typedef void (*link_msg_cb)(void *ctx, link_t *l, const mavlink_message_t *msg);
/* Handle poll() results: reads + parses (calling cb per message) and flushes
 * pending output. Returns false if the link died (l->err set, fd closed). */
bool link_service(link_t *l, short revents, link_msg_cb cb, void *ctx);

/* Serialize and queue a packed message. Returns false if it was dropped. */
bool link_send(link_t *l, const mavlink_message_t *msg);

/* Use MAVLink 1 framing for output (peer only speaks v1). */
void link_set_mavlink1(link_t *l, bool v1);
