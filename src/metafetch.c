#include "metafetch.h"

#include <curl/curl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "i18n.h"
#include "util.h"

#define APM_BASE "https://autotest.ardupilot.org/Parameters"
#define PX4_URL                                                                                                                            \
	"https://raw.githubusercontent.com/mavlink/qgroundcontrol/master/src/FirmwarePlugin/"                                                  \
	"PX4/PX4ParameterFactMetaData.json"
#define PX4_TR_URL "https://raw.githubusercontent.com/PX4/PX4-Metadata-Translations/main/translated/parameters_%s.ts"
#define MAX_AGE_S (7 * 24 * 3600)

void metafetch_global_init(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }
void metafetch_global_cleanup(void) { curl_global_cleanup(); }

static atomic_int g_cancel;

static int progress_cb(void *p, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) { return atomic_load(&g_cancel) ? 1 : 0; }

static bool download(const char *url, const char *dest, char *err, size_t errlen) {
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.part", dest);
	FILE *f = fopen(tmp, "wb");
	if (!f) {
		snprintf(err, errlen, "%s: %s", tmp, strerror(errno));
		return false;
	}
	CURL *c					   = curl_easy_init();
	char ebuf[CURL_ERROR_SIZE] = "";
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "gndtui/" GNDTUI_VERSION);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, ""); /* gzip: the .ts files compress 10x */
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, ebuf);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
	CURLcode rc = curl_easy_perform(c);
	curl_easy_cleanup(c);
	bool ok = fclose(f) == 0 && rc == CURLE_OK;
	if (!ok) {
		snprintf(err, errlen, "%s", ebuf[0] ? ebuf : curl_easy_strerror(rc));
		unlink(tmp);
		return false;
	}
	if (rename(tmp, dest) < 0) {
		snprintf(err, errlen, "rename %s: %s", dest, strerror(errno));
		unlink(tmp);
		return false;
	}
	LOG("metadata: downloaded %s -> %s", url, dest);
	return true;
}

typedef struct source {
	char url[512];
	char file[1024];
	char label[128];
	bool immutable;
} source_t;

/* Make sure s->file is usable: fresh cache, or download, or stale cache.
 * Returns false if there is no file at all. *how describes where it came from. */
static bool ensure_file(const source_t *s, bool offline, const char **how, char *err, size_t errlen) {
	struct stat st;
	bool cached = stat(s->file, &st) == 0 && st.st_size > 0;
	bool fresh	= cached && (s->immutable || time(NULL) - st.st_mtime < MAX_AGE_S);
	*how		= _("cached");
	if (!fresh && !offline) {
		if (download(s->url, s->file, err, errlen)) {
			*how = _("downloaded");
			return true;
		}
		LOG("metadata: download %s failed: %s", s->url, err);
		*how = _("stale cache");
	}
	return cached;
}

static meta_db_t *try_load(const char *path, char *err, size_t errlen) {
	meta_db_t *db = meta_db_new();
	if (meta_load_file(db, path, err, errlen))
		return db;
	meta_db_free(db);
	return NULL;
}

/* PX4 community translations (Crowdin). Missing language = silently none. */
static void load_translation(metafetch_t *mf, const char *cache) {
	const metafetch_req_t *r = &mf->req;
	source_t s				 = {0};
	snprintf(s.url, sizeof s.url, PX4_TR_URL, r->lang);
	snprintf(s.file, sizeof s.file, "%s/px4-parameters_%s.ts", cache, r->lang);
	const char *how;
	char err[512] = "";
	if (!ensure_file(&s, r->offline, &how, err, sizeof err))
		return;
	if (!meta_load_qt_ts(mf->result, s.file, r->lang, err, sizeof err)) {
		LOG("metadata: translation %s: %s", s.file, err);
		return;
	}
	LOG("metadata: translation %s: %d strings applied, %d stale", r->lang, mf->result->tr_strings, mf->result->tr_stale);
	if (mf->result->tr_strings == 0) {
		mf->result->tr_lang[0] = '\0';
		return;
	}
	size_t l = strlen(mf->msg);
	snprintf(mf->msg + l, sizeof mf->msg - l, _("; translation %s: %d strings"), r->lang, mf->result->tr_strings);
}

static void *worker(void *arg) {
	metafetch_t *mf			 = arg;
	const metafetch_req_t *r = &mf->req;
	char err[512]			 = "";

	if (r->override_path[0]) {
		mf->result = try_load(r->override_path, err, sizeof err);
		if (mf->result)
			snprintf(mf->result->source, sizeof mf->result->source, "%s", r->override_path);
		snprintf(mf->msg, sizeof mf->msg, "%s", mf->result ? "metadata loaded" : err);
		atomic_store(&mf->done, 1);
		return NULL;
	}

	char cache[900];
	if (!path_cache_dir(cache, sizeof cache)) {
		snprintf(mf->msg, sizeof mf->msg, "%s", _("metadata: no cache directory (HOME unset?)"));
		atomic_store(&mf->done, 1);
		return NULL;
	}

	source_t src[2];
	int nsrc = 0;
	if (r->ardupilot) {
		if (r->version[0] && r->apm_versioned[0]) {
			source_t *s = &src[nsrc++];
			snprintf(s->url, sizeof s->url, APM_BASE "/versioned/%s/stable-%s/apm.pdef.xml", r->apm_versioned, r->version);
			snprintf(s->file, sizeof s->file, "%s/apm-%s-%s.pdef.xml", cache, r->apm_versioned, r->version);
			snprintf(s->label, sizeof s->label, "ArduPilot %s %s", r->apm_versioned, r->version);
			s->immutable = true;
		}
		source_t *s = &src[nsrc++];
		snprintf(s->url, sizeof s->url, APM_BASE "/%s/apm.pdef.xml", r->apm_master);
		snprintf(s->file, sizeof s->file, "%s/apm-%s-master.pdef.xml", cache, r->apm_master);
		snprintf(s->label, sizeof s->label, "ArduPilot %s (master)", r->apm_master);
	} else {
		source_t *s = &src[nsrc++];
		snprintf(s->url, sizeof s->url, "%s", PX4_URL);
		snprintf(s->file, sizeof s->file, "%s/px4-parameters.json", cache);
		snprintf(s->label, sizeof s->label, "PX4 (master)");
	}

	for (int i = 0; i < nsrc && !mf->result && !atomic_load(&g_cancel); i++) {
		source_t *s = &src[i];
		const char *how;
		if (!ensure_file(s, r->offline, &how, err, sizeof err))
			continue;
		mf->result = try_load(s->file, err, sizeof err);
		if (mf->result) {
			snprintf(mf->result->source, sizeof mf->result->source, "%s", s->label);
			snprintf(mf->msg, sizeof mf->msg, _("metadata: %s, %d params (%s)"), s->label, mf->result->n, how);
		}
	}
	if (!mf->result)
		snprintf(mf->msg, sizeof mf->msg, _("metadata unavailable: %s"),
				err[0] ? err : (r->offline ? _("offline and nothing cached") : _("unknown error")));
	else if (!r->ardupilot && r->lang[0])
		load_translation(mf, cache);
	atomic_store(&mf->done, 1);
	return NULL;
}

bool metafetch_start(metafetch_t *mf, const metafetch_req_t *req) {
	memset(mf, 0, sizeof *mf);
	mf->req = *req;
	atomic_store(&g_cancel, 0);
	if (pthread_create(&mf->th, NULL, worker, mf) != 0) {
		snprintf(mf->msg, sizeof mf->msg, "%s", _("metadata: cannot start thread"));
		return false;
	}
	mf->started = true;
	return true;
}

bool metafetch_poll(metafetch_t *mf, meta_db_t **out, char *msg, size_t n) {
	if (!mf->started || !atomic_load(&mf->done))
		return false;
	pthread_join(mf->th, NULL);
	mf->started = false;
	*out		= mf->result;
	mf->result	= NULL;
	snprintf(msg, n, "%s", mf->msg);
	return true;
}

void metafetch_cancel(metafetch_t *mf) {
	if (!mf->started)
		return;
	atomic_store(&g_cancel, 1);
	pthread_join(mf->th, NULL);
	mf->started = false;
	meta_db_free(mf->result);
	mf->result = NULL;
}
