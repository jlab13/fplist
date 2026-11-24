#include "link.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "util.h"

/* ------------------------------------------------------------------------ */
/* connection strings                                                         */

static bool parse_port(const char *s, int *port) {
	char *end;
	long v = strtol(s, &end, 10);
	if (end == s || *end || v <= 0 || v > 65535)
		return false;
	*port = (int)v;
	return true;
}

/* "host:port", ":port" or "port" (host defaults to def_host). */
static bool parse_hostport(const char *s, const char *def_host, link_spec_t *out) {
	const char *colon = strrchr(s, ':');
	if (!colon) {
		xstrlcpy(out->host, def_host, sizeof out->host);
		return parse_port(s, &out->port);
	}
	size_t hl = (size_t)(colon - s);
	if (hl == 0) {
		xstrlcpy(out->host, def_host, sizeof out->host);
	} else {
		if (hl >= sizeof out->host)
			return false;
		memcpy(out->host, s, hl);
		out->host[hl] = '\0';
	}
	return parse_port(colon + 1, &out->port);
}

bool link_spec_parse(const char *uri, int default_baud, link_spec_t *out, char *err, size_t errlen) {
	memset(out, 0, sizeof *out);
	out->baud = default_baud;

	struct {
		const char *prefix;
		link_kind_t kind;
		const char *def_host;
	} nets[] = {
			{"udpin:",	LINK_UDP_IN,  "0.0.0.0"	 },
			{"udpout:", LINK_UDP_OUT, "127.0.0.1"},
			{"udp:",	LINK_UDP_IN,  "0.0.0.0"	 },
			{"tcp:",	LINK_TCP,	  "127.0.0.1"},
	};
	for (size_t i = 0; i < ARRAY_LEN(nets); i++) {
		size_t pl = strlen(nets[i].prefix);
		if (strncmp(uri, nets[i].prefix, pl) == 0) {
			out->kind = nets[i].kind;
			if (!parse_hostport(uri + pl, nets[i].def_host, out)) {
				snprintf(err, errlen, "bad address in '%s' (expected host:port)", uri);
				return false;
			}
			return true;
		}
	}

	const char *dev = uri;
	if (strncmp(dev, "serial:", 7) == 0)
		dev += 7;
	if (!*dev) {
		snprintf(err, errlen, "empty serial device");
		return false;
	}
	out->kind = LINK_SERIAL;
	xstrlcpy(out->host, dev, sizeof out->host);
	/* optional baud suffix: /dev/ttyUSB0:57600, /dev/ttyUSB0,57600, @57600 */
	/* (only after the last separator, and only if it is all digits: udev
	 * by-path names like ...-usb-0:2:1.0 contain colons) */
	char *sep = NULL;
	for (char *c = out->host; *c; c++)
		if (*c == ':' || *c == ',' || *c == '@')
			sep = c;
	if (sep && sep[1] && strspn(sep + 1, "0123456789") == strlen(sep + 1)) {
		long v = strtol(sep + 1, NULL, 10);
		if (v <= 0 || v > 20000000) {
			snprintf(err, errlen, "bad baud rate in '%s'", uri);
			return false;
		}
		*sep	  = '\0';
		out->baud = (int)v;
	}
	return true;
}

void link_spec_describe(const link_spec_t *s, char *out, size_t n) {
	switch (s->kind) {
	case LINK_SERIAL:
		snprintf(out, n, "%s @%d", s->host, s->baud);
		break;
	case LINK_UDP_IN:
		snprintf(out, n, "udp:%s:%d", s->host, s->port);
		break;
	case LINK_UDP_OUT:
		snprintf(out, n, "udpout:%s:%d", s->host, s->port);
		break;
	case LINK_TCP:
		snprintf(out, n, "tcp:%s:%d", s->host, s->port);
		break;
	default:
		snprintf(out, n, "(none)");
	}
}

/* ------------------------------------------------------------------------ */
/* open / close                                                               */

void link_init(link_t *l) {
	memset(l, 0, sizeof *l);
	l->fd = -1;
}

static speed_t baud_to_speed(int baud) {
	switch (baud) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
#ifdef B460800
	case 460800:
		return B460800;
#endif
#ifdef B500000
	case 500000:
		return B500000;
#endif
#ifdef B921600
	case 921600:
		return B921600;
#endif
#ifdef B1000000
	case 1000000:
		return B1000000;
#endif
#ifdef B1500000
	case 1500000:
		return B1500000;
#endif
#ifdef B2000000
	case 2000000:
		return B2000000;
#endif
	default:
		return 0;
	}
}

static bool fail(link_t *l, const char *what) {
	snprintf(l->err, sizeof l->err, "%s: %s", what, strerror(errno));
	if (l->fd >= 0)
		close(l->fd);
	l->fd = -1;
	return false;
}

static bool open_serial(link_t *l) {
	speed_t sp = baud_to_speed(l->spec.baud);
	if (!sp) {
		snprintf(l->err, sizeof l->err, "unsupported baud rate %d", l->spec.baud);
		return false;
	}
	l->fd = open(l->spec.host, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (l->fd < 0)
		return fail(l, "open");
	if (ioctl(l->fd, TIOCEXCL) < 0)
		return fail(l, "TIOCEXCL");

	struct termios t;
	if (tcgetattr(l->fd, &t) < 0)
		return fail(l, "tcgetattr");
	cfmakeraw(&t);
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB);
	t.c_cc[VMIN]  = 0;
	t.c_cc[VTIME] = 0;
	cfsetispeed(&t, sp);
	cfsetospeed(&t, sp);
	if (tcsetattr(l->fd, TCSANOW, &t) < 0)
		return fail(l, "tcsetattr");
	tcflush(l->fd, TCIOFLUSH);
	return true;
}

static bool resolve(link_t *l, int socktype, struct sockaddr_storage *sa, socklen_t *salen) {
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family	  = AF_INET; /* MAVLink setups are practically always IPv4 */
	hints.ai_socktype = socktype;
	char port[16];
	snprintf(port, sizeof port, "%d", l->spec.port);
	int rc = getaddrinfo(l->spec.host, port, &hints, &res);
	if (rc || !res) {
		snprintf(l->err, sizeof l->err, "resolve %s: %s", l->spec.host, gai_strerror(rc));
		return false;
	}
	memcpy(sa, res->ai_addr, res->ai_addrlen);
	*salen = res->ai_addrlen;
	freeaddrinfo(res);
	return true;
}

static bool open_udp(link_t *l) {
	struct sockaddr_storage sa;
	socklen_t salen;
	if (!resolve(l, SOCK_DGRAM, &sa, &salen))
		return false;
	l->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (l->fd < 0)
		return fail(l, "socket");
	if (l->spec.kind == LINK_UDP_IN) {
		int one = 1;
		setsockopt(l->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		if (bind(l->fd, (struct sockaddr *)&sa, salen) < 0)
			return fail(l, "bind");
	} else {
		memcpy(&l->peer, &sa, salen);
		l->peer_len	  = salen;
		l->peer_known = true;
	}
	return true;
}

static bool open_tcp(link_t *l) {
	struct sockaddr_storage sa;
	socklen_t salen;
	if (!resolve(l, SOCK_STREAM, &sa, &salen))
		return false;
	l->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (l->fd < 0)
		return fail(l, "socket");
	int one = 1;
	setsockopt(l->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	if (connect(l->fd, (struct sockaddr *)&sa, salen) < 0) {
		if (errno != EINPROGRESS)
			return fail(l, "connect");
		l->connecting = true;
	}
	return true;
}

bool link_open(link_t *l, const link_spec_t *spec) {
	if (l->fd >= 0)
		link_close(l);
	link_spec_t s = *spec;
	link_init(l);
	l->spec = s;
	link_spec_describe(&l->spec, l->name, sizeof l->name);
	switch (s.kind) {
	case LINK_SERIAL:
		return open_serial(l);
	case LINK_UDP_IN:
	case LINK_UDP_OUT:
		return open_udp(l);
	case LINK_TCP:
		return open_tcp(l);
	default:
		snprintf(l->err, sizeof l->err, "no link type");
		return false;
	}
}

void link_close(link_t *l) {
	if (l->fd >= 0)
		close(l->fd);
	l->fd		  = -1;
	l->connecting = false;
	l->tx_len	  = 0;
}

void link_set_mavlink1(link_t *l, bool v1) {
	if (v1)
		l->tx_status.flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
	else
		l->tx_status.flags &= (uint8_t)~MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
}

/* ------------------------------------------------------------------------ */
/* I/O                                                                        */

short link_poll_events(const link_t *l) {
	if (l->fd < 0)
		return 0;
	if (l->connecting)
		return POLLOUT;
	return (short)(POLLIN | (l->tx_len ? POLLOUT : 0));
}

static bool flush_tx(link_t *l) {
	while (l->tx_len) {
		ssize_t w = write(l->fd, l->tx, l->tx_len);
		if (w < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				return true;
			return fail(l, "write");
		}
		l->tx_bytes += (uint64_t)w;
		memmove(l->tx, l->tx + w, l->tx_len - (size_t)w);
		l->tx_len -= (size_t)w;
	}
	return true;
}

static void parse_bytes(link_t *l, const uint8_t *buf, size_t n, link_msg_cb cb, void *ctx) {
	l->rx_bytes += n;
	for (size_t i = 0; i < n; i++) {
		mavlink_message_t msg;
		mavlink_status_t st;
		uint8_t r = mavlink_parse_char_buffer(&l->rx_msg, &l->rx_status, buf[i], &msg, &st);
		if (r == MAVLINK_FRAMING_OK) {
			l->rx_msgs++;
			if (cb)
				cb(ctx, l, &msg);
			if (l->fd < 0) /* callback closed us */
				return;
		} else if (r == MAVLINK_FRAMING_BAD_CRC) {
			l->rx_crc_err++;
		}
	}
}

bool link_service(link_t *l, short revents, link_msg_cb cb, void *ctx) {
	if (l->fd < 0)
		return false;

	if (l->connecting) {
		if (!(revents & (POLLOUT | POLLERR | POLLHUP)))
			return true;
		int e		 = 0;
		socklen_t el = sizeof e;
		getsockopt(l->fd, SOL_SOCKET, SO_ERROR, &e, &el);
		if (e) {
			errno = e;
			return fail(l, "connect");
		}
		l->connecting = false;
		return true;
	}

	if (revents & POLLIN) {
		uint8_t buf[4096];
		for (int guard = 0; guard < 64; guard++) {
			ssize_t r;
			if (l->spec.kind == LINK_UDP_IN || l->spec.kind == LINK_UDP_OUT) {
				struct sockaddr_storage from;
				socklen_t fl = sizeof from;
				r			 = recvfrom(l->fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
				if (r > 0 && l->spec.kind == LINK_UDP_IN) {
					memcpy(&l->peer, &from, fl);
					l->peer_len	  = fl;
					l->peer_known = true;
				}
			} else {
				r = read(l->fd, buf, sizeof buf);
			}
			if (r > 0) {
				parse_bytes(l, buf, (size_t)r, cb, ctx);
				if (l->fd < 0)
					return false;
				continue;
			}
			if (r == 0 && l->spec.kind != LINK_UDP_IN && l->spec.kind != LINK_UDP_OUT) {
				if (l->spec.kind == LINK_SERIAL) {
					if (revents & (POLLHUP | POLLERR)) {
						errno = EIO;
						return fail(l, "device lost");
					}
					break; /* VMIN=0: nothing more right now */
				}
				errno = ECONNRESET;
				return fail(l, "connection closed");
			}
			if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
				break;
			if (r < 0) {
				if (errno == ECONNREFUSED && l->spec.kind == LINK_UDP_OUT)
					break; /* nobody listening yet: keep trying */
				return fail(l, "read");
			}
			break;
		}
	} else if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
		errno = (revents & POLLNVAL) ? EBADF : EIO;
		return fail(l, "device lost");
	}

	if (l->tx_len)
		return flush_tx(l);
	return true;
}

bool link_send(link_t *l, const mavlink_message_t *msg) {
	if (l->fd < 0 || l->connecting)
		return false;
	uint8_t buf[MAVLINK_MAX_PACKET_LEN];
	uint16_t len = mavlink_msg_to_send_buffer(buf, msg);

	if (l->spec.kind == LINK_UDP_IN || l->spec.kind == LINK_UDP_OUT) {
		if (!l->peer_known)
			return false;
		ssize_t w = sendto(l->fd, buf, len, 0, (struct sockaddr *)&l->peer, l->peer_len);
		if (w < 0) {
			l->tx_dropped++;
			return false;
		}
		l->tx_bytes += (uint64_t)w;
		return true;
	}

	if (l->tx_len + len > sizeof l->tx) {
		l->tx_dropped++;
		return false;
	}
	memcpy(l->tx + l->tx_len, buf, len);
	l->tx_len += len;
	return flush_tx(l);
}
