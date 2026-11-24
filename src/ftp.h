/* MAVLink FTP (https://mavlink.io/en/services/ftp.html): read-only download
 * of one file into memory, as QGroundControl / Mission Planner do for
 * ArduPilot's "@PARAM/param.pck?withdefaults=1".
 *
 * OpenFileRO -> BurstReadFile (streamed) -> ReadFile for any gaps left by
 * lost packets -> TerminateSession. Requests are retried on timeout; the
 * caller falls back to another method when the download fails. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FTP_PAYLOAD_LEN 251
#define FTP_DATA_MAX 239

typedef bool (*ftp_send_fn)(void *ctx, const uint8_t payload[FTP_PAYLOAD_LEN]);

typedef enum { FTP_IDLE, FTP_OPENING, FTP_RESETTING, FTP_READING, FTP_DONE, FTP_FAILED } ftp_state_t;

typedef struct ftp {
	ftp_state_t st;
	ftp_send_fn send;
	void *ctx;

	char path[128];
	uint16_t seq;
	uint8_t session;
	uint8_t last_req[FTP_PAYLOAD_LEN]; /* for resends */

	uint8_t *buf;  /* file contents */
	uint8_t *have; /* per byte: received */
	uint32_t cap;
	uint32_t hi;	   /* highest received byte + 1 */
	int64_t size;	   /* file size, known once EOF was reached, else -1 */
	int64_t size_hint; /* size from the open reply: ArduPilot reports an upper
						* estimate for generated files, so only for progress */
	uint32_t received; /* bytes received (unique) */

	double started, last_rx, sent_at;
	int retries;
	bool reset_tried;
	bool not_found; /* the vehicle has no such file (don't retry later) */
	char err[128];
} ftp_t;

void ftp_init(ftp_t *f, ftp_send_fn send, void *ctx);
void ftp_free(ftp_t *f);
/* Start downloading `path`. */
void ftp_start(ftp_t *f, const char *path, double now);
/* Feed the payload of a FILE_TRANSFER_PROTOCOL message from the vehicle. */
void ftp_handle(ftp_t *f, const uint8_t payload[FTP_PAYLOAD_LEN], double now);
/* Timeouts / retries; call regularly. */
void ftp_tick(ftp_t *f, double now);
/* Abort (sends TerminateSession if a session is open). */
void ftp_cancel(ftp_t *f);

static inline bool ftp_busy(const ftp_t *f) { return f->st == FTP_OPENING || f->st == FTP_RESETTING || f->st == FTP_READING; }
