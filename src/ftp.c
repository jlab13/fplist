#include "ftp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* payload layout */
enum { P_SEQ = 0, P_SESSION = 2, P_OPCODE = 3, P_SIZE = 4, P_REQ_OPCODE = 5, P_BURST_COMPLETE = 6, P_OFFSET = 8, P_DATA = 12 };

enum {
	OP_NONE		  = 0,
	OP_TERMINATE  = 1,
	OP_RESET	  = 2,
	OP_OPEN_RO	  = 4,
	OP_READ		  = 5,
	OP_BURST_READ = 15,
	OP_ACK		  = 128,
	OP_NAK		  = 129,
};

enum { ERR_FAIL = 1, ERR_FAIL_ERRNO = 2, ERR_NO_SESSIONS = 5, ERR_EOF = 6, ERR_FILE_NOT_FOUND = 10 };

#define FTP_TIMEOUT 1.0 /* no reply / no burst data */
#define FTP_RETRIES 6
#define FTP_MAX_TIME 60.0 /* give up on hopelessly slow links */
#define FTP_MAX_SIZE (4u << 20)

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void wr32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

void ftp_init(ftp_t *f, ftp_send_fn send, void *ctx) {
	memset(f, 0, sizeof *f);
	f->send		 = send;
	f->ctx		 = ctx;
	f->size		 = -1;
	f->size_hint = -1;
}

void ftp_free(ftp_t *f) {
	free(f->buf);
	free(f->have);
	f->buf = f->have = NULL;
	f->cap			 = 0;
}

static void request(ftp_t *f, uint8_t opcode, uint8_t session, uint32_t offset, uint8_t size, const void *data, size_t dlen, double now) {
	uint8_t *p = f->last_req;
	memset(p, 0, FTP_PAYLOAD_LEN);
	f->seq++;
	p[P_SEQ]	 = (uint8_t)f->seq;
	p[P_SEQ + 1] = (uint8_t)(f->seq >> 8);
	p[P_SESSION] = session;
	p[P_OPCODE]	 = opcode;
	p[P_SIZE]	 = size;
	wr32(p + P_OFFSET, offset);
	if (data && dlen)
		memcpy(p + P_DATA, data, MIN(dlen, (size_t)FTP_DATA_MAX));
	f->sent_at = now;
	f->send(f->ctx, p);
}

static void resend(ftp_t *f, double now) {
	/* a fresh sequence number, otherwise identical */
	f->seq++;
	f->last_req[P_SEQ]	   = (uint8_t)f->seq;
	f->last_req[P_SEQ + 1] = (uint8_t)(f->seq >> 8);
	f->sent_at			   = now;
	f->send(f->ctx, f->last_req);
}

static void fail(ftp_t *f, const char *why) {
	if (f->st == FTP_READING)
		request(f, OP_TERMINATE, f->session, 0, 0, NULL, 0, f->sent_at);
	f->st = FTP_FAILED;
	snprintf(f->err, sizeof f->err, "%s", why);
	LOG("ftp: %s failed: %s", f->path, why);
}

void ftp_start(ftp_t *f, const char *path, double now) {
	ftp_free(f);
	ftp_send_fn send = f->send;
	void *ctx		 = f->ctx;
	uint16_t seq	 = f->seq;
	ftp_init(f, send, ctx);
	f->seq = seq;
	xstrlcpy(f->path, path, sizeof f->path);
	f->st	   = FTP_OPENING;
	f->started = f->last_rx = now;
	size_t len				= strlen(path);
	request(f, OP_OPEN_RO, 0, 0, (uint8_t)len, path, len, now);
	LOG("ftp: open %s", path);
}

void ftp_cancel(ftp_t *f) {
	if (f->st == FTP_READING)
		request(f, OP_TERMINATE, f->session, 0, 0, NULL, 0, f->sent_at);
	if (ftp_busy(f)) {
		f->st = FTP_FAILED;
		snprintf(f->err, sizeof f->err, "cancelled");
	}
}

static bool grow(ftp_t *f, uint32_t need) {
	if (need <= f->cap)
		return true;
	if (need > FTP_MAX_SIZE)
		return false;
	uint32_t cap = f->cap ? f->cap : 16384;
	while (cap < need)
		cap *= 2;
	f->buf	= xrealloc(f->buf, cap);
	f->have = xrealloc(f->have, cap);
	memset(f->have + f->cap, 0, cap - f->cap);
	f->cap = cap;
	return true;
}

/* First byte not yet received below `limit`, or `limit`. */
static uint32_t first_gap(const ftp_t *f, uint32_t limit) {
	for (uint32_t i = 0; i < limit && i < f->cap; i++)
		if (!f->have[i])
			return i;
	return limit;
}

static void finish(ftp_t *f) {
	request(f, OP_TERMINATE, f->session, 0, 0, NULL, 0, f->sent_at);
	f->st = FTP_DONE;
	LOG("ftp: %s done, %lld bytes", f->path, (long long)f->size);
}

/* Decide what to ask for next. `burst_ended`: the vehicle finished a burst. */
static void next(ftp_t *f, double now, bool burst_ended) {
	if (f->size >= 0) {
		uint32_t gap = first_gap(f, (uint32_t)f->size);
		if (gap >= (uint32_t)f->size) {
			/* all up to the end a burst's EOF showed: ask past it, the EOF
			 * of a plain read there confirms it */
			if (f->size_unsure)
				request(f, OP_READ, f->session, (uint32_t)f->size, FTP_DATA_MAX, NULL, 0, now);
			else
				finish(f);
			return;
		}
		/* holes: fill with plain reads; a big remainder: another burst */
		uint32_t left = (uint32_t)f->size - gap;
		bool tail	  = gap >= f->hi;
		if (tail && left > FTP_DATA_MAX)
			request(f, OP_BURST_READ, f->session, gap, FTP_DATA_MAX, NULL, 0, now);
		else
			request(f, OP_READ, f->session, gap, (uint8_t)MIN(left, (uint32_t)FTP_DATA_MAX), NULL, 0, now);
		return;
	}
	if (burst_ended) /* size unknown: continue streaming after what we have */
		request(f, OP_BURST_READ, f->session, first_gap(f, f->hi) < f->hi ? first_gap(f, f->hi) : f->hi, FTP_DATA_MAX, NULL, 0, now);
}

void ftp_handle(ftp_t *f, const uint8_t p[FTP_PAYLOAD_LEN], double now) {
	if (!ftp_busy(f))
		return;
	uint8_t opcode = p[P_OPCODE], req = p[P_REQ_OPCODE], size = p[P_SIZE];
	if (opcode != OP_ACK && opcode != OP_NAK)
		return;

	if (f->st == FTP_RESETTING) {
		if (req == OP_RESET) {
			f->st	   = FTP_OPENING;
			size_t len = strlen(f->path);
			request(f, OP_OPEN_RO, 0, 0, (uint8_t)len, f->path, len, now);
		}
		return;
	}

	if (f->st == FTP_OPENING) {
		if (req != OP_OPEN_RO)
			return;
		f->last_rx = now;
		if (opcode == OP_NAK) {
			uint8_t e = size ? p[P_DATA] : ERR_FAIL;
			if (e == ERR_NO_SESSIONS && !f->reset_tried) {
				f->reset_tried = true;
				f->st		   = FTP_RESETTING;
				request(f, OP_RESET, 0, 0, 0, NULL, 0, now);
				return;
			}
			bool enoent	 = e == ERR_FAIL_ERRNO && size >= 2 && p[P_DATA + 1] == 2;
			f->not_found = e == ERR_FILE_NOT_FOUND || enoent;
			char why[64];
			snprintf(why, sizeof why, f->not_found ? "file not found" : "open refused (error %u)", e);
			f->st = FTP_FAILED;
			xstrlcpy(f->err, why, sizeof f->err);
			LOG("ftp: open %s: %s", f->path, why);
			return;
		}
		f->session = p[P_SESSION];
		if (size >= 4) {
			uint32_t sz = rd32(p + P_DATA);
			if (sz > 0 && sz <= FTP_MAX_SIZE)
				f->size_hint = sz;
		}
		f->st	   = FTP_READING;
		f->retries = 0;
		LOG("ftp: session %u, size hint %lld", f->session, (long long)f->size_hint);
		request(f, OP_BURST_READ, f->session, 0, FTP_DATA_MAX, NULL, 0, now);
		return;
	}

	/* FTP_READING */
	if (p[P_SESSION] != f->session || (req != OP_BURST_READ && req != OP_READ))
		return;
	f->last_rx = now;
	f->retries = 0;
	if (opcode == OP_NAK) {
		uint8_t e = size ? p[P_DATA] : ERR_FAIL;
		if (e != ERR_EOF) {
			char why[64];
			snprintf(why, sizeof why, "read error %u", e);
			fail(f, why);
			return;
		}
		/* EOF is the only reliable end: the open reply's size may be an
		 * estimate. A plain read's EOF at offset X: the file is X bytes. A
		 * burst's: it ends at the last byte that arrived, unless the last
		 * packets were lost, so that is checked (next()) */
		if (req == OP_READ) {
			f->size		   = MAX(rd32(f->last_req + P_OFFSET), f->hi);
			f->size_unsure = false;
		} else if (f->size < 0 || f->hi > f->size) {
			f->size		   = f->hi;
			f->size_unsure = true;
		}
		next(f, now, true);
		return;
	}
	uint32_t off = rd32(p + P_OFFSET);
	if (size > FTP_DATA_MAX || !grow(f, off + size)) {
		fail(f, "bad data packet / file too large");
		return;
	}
	memcpy(f->buf + off, p + P_DATA, size);
	for (uint32_t i = off; i < off + size; i++)
		if (!f->have[i]) {
			f->have[i] = 1;
			f->received++;
		}
	if (off + size > f->hi)
		f->hi = off + size;
	/* data past the end an EOF suggested: its tail had been lost, the end is
	 * unknown again */
	if (f->size >= 0 && off + size > (uint32_t)f->size)
		f->size = -1;
	/* plain read replies and the end of a burst drive the next request */
	if (req == OP_READ || p[P_BURST_COMPLETE])
		next(f, now, true);
	else if (f->size >= 0 && f->received >= (uint32_t)f->size)
		next(f, now, false); /* done, or the end to confirm */
}

void ftp_tick(ftp_t *f, double now) {
	if (!ftp_busy(f))
		return;
	if (now - f->started > FTP_MAX_TIME) {
		fail(f, "too slow");
		return;
	}
	if (now - f->last_rx < FTP_TIMEOUT || now - f->sent_at < FTP_TIMEOUT)
		return;
	if (++f->retries > FTP_RETRIES) {
		fail(f, f->st == FTP_OPENING ? "no reply" : "timeout");
		return;
	}
	LOG("ftp: timeout, retry %d (%u bytes so far)", f->retries, f->received);
	if (f->st == FTP_READING && (f->received || f->size >= 0))
		next(f, now, true); /* re-request from the first missing byte */
	else
		resend(f, now);
}
