#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "employee_sync.h"
#include "storage.h"
#include "http_client.h"
#include "server_config.h"

LOG_MODULE_REGISTER(emp_sync, LOG_LEVEL_INF);

#define SYNC_HOST      APP_SERVER_HOST
#define SYNC_PORT      APP_SERVER_PORT
#define SYNC_PATH_FMT  "/employees?last_sync=%llu"

#define LAST_SYNC_FILE "last_sync.txt"
#define ACCESS_FILE    "access_list.txt"

#define MAX_CHANGES    32   /* max records the server may return per sync */
#define MAX_UIDS       200  /* max UIDs kept in the local whitelist        */
#define MAX_UID_LEN    24

/* ---------- In-RAM mirror of the on-flash whitelist ---------- */
static char whitelist[MAX_UIDS][MAX_UID_LEN];
static size_t whitelist_len;
static bool whitelist_loaded;

/* Protects whitelist[]/whitelist_len against concurrent access from the
 * periodic sync thread and the RFID scan loop (a different thread). */
K_MUTEX_DEFINE(whitelist_mutex);

/* ---------- Wire format for the server's delta response ---------- */
struct access_change {
	const char *rfid_uid;
	int64_t updated_at;
	bool deleted;
};

struct sync_response {
	int64_t server_time;
	struct access_change changes[MAX_CHANGES];
	size_t changes_len;
};

static const struct json_obj_descr change_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct access_change, rfid_uid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct access_change, updated_at, JSON_TOK_INT64),
	JSON_OBJ_DESCR_PRIM(struct access_change, deleted, JSON_TOK_TRUE),
};

static const struct json_obj_descr response_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct sync_response, server_time, JSON_TOK_INT64),
	JSON_OBJ_DESCR_OBJ_ARRAY(struct sync_response, changes, MAX_CHANGES,
				  changes_len, change_descr, ARRAY_SIZE(change_descr)),
};

/* ---------- Whitelist (de)serialization: one UID per line ----------
 * Callers must hold whitelist_mutex.
 */
static void whitelist_load_locked(void)
{
	static char buf[MAX_UIDS * (MAX_UID_LEN + 1)];
	int ret;

	whitelist_len = 0;
	whitelist_loaded = true;

	ret = storage_load(ACCESS_FILE, buf, sizeof(buf) - 1);
	if (ret <= 0) {
		return; /* no whitelist yet: empty, not an error */
	}
	buf[ret] = '\0';

	char *saveptr = NULL;
	char *line = strtok_r(buf, "\n", &saveptr);

	while (line && whitelist_len < MAX_UIDS) {
		if (line[0] != '\0') {
			strncpy(whitelist[whitelist_len], line, MAX_UID_LEN - 1);
			whitelist[whitelist_len][MAX_UID_LEN - 1] = '\0';
			whitelist_len++;
		}
		line = strtok_r(NULL, "\n", &saveptr);
	}
}

static int whitelist_save_locked(void)
{
	static char buf[MAX_UIDS * (MAX_UID_LEN + 1)];
	size_t off = 0;

	for (size_t i = 0; i < whitelist_len; i++) {
		int n = snprintk(buf + off, sizeof(buf) - off, "%s\n", whitelist[i]);

		if (n < 0 || (size_t)n >= sizeof(buf) - off) {
			LOG_ERR("Whitelist buffer full, truncating save at %zu/%zu",
				i, whitelist_len);
			break;
		}
		off += (size_t)n;
	}
	return storage_save(ACCESS_FILE, buf, off);
}

static int whitelist_find_locked(const char *uid)
{
	for (size_t i = 0; i < whitelist_len; i++) {
		if (strcmp(whitelist[i], uid) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static void whitelist_remove_at_locked(size_t idx)
{
	whitelist[idx][0] = '\0';
	memcpy(whitelist[idx], whitelist[whitelist_len - 1], MAX_UID_LEN);
	whitelist_len--;
}

/* search-and-add / search-and-delete */
static int whitelist_apply_change_locked(const struct access_change *c)
{
	int idx = whitelist_find_locked(c->rfid_uid);

	if (c->deleted) {
		if (idx >= 0) {
			whitelist_remove_at_locked((size_t)idx);
		}
		return 0;
	}

	if (idx >= 0) {
		return 0; /* already authorized, nothing to do */
	}

	if (whitelist_len >= MAX_UIDS) {
		LOG_ERR("Whitelist full (%d), dropping UID %s", MAX_UIDS, c->rfid_uid);
		return -ENOMEM;
	}

	strncpy(whitelist[whitelist_len], c->rfid_uid, MAX_UID_LEN - 1);
	whitelist[whitelist_len][MAX_UID_LEN - 1] = '\0';
	whitelist_len++;
	return 0;
}

/* ---------- Public API ---------- */

int employees_sync(void)
{
	static uint8_t http_buf[4096]; /* size to your expected delta payload */
	uint64_t last_sync = 0;
	int ret;

	storage_load_u64(LAST_SYNC_FILE, &last_sync);

	k_mutex_lock(&whitelist_mutex, K_FOREVER);
	if (!whitelist_loaded) {
		whitelist_load_locked();
	}
	k_mutex_unlock(&whitelist_mutex);

	/* Loop (not recurse) through truncated batches: a large backlog could
	 * otherwise mean dozens of nested stack frames. */
	for (;;) {
		char path[80];

		snprintk(path, sizeof(path), SYNC_PATH_FMT, (unsigned long long)last_sync);

		LOG_INF("Syncing access list since cursor=%llu ...",
			(unsigned long long)last_sync);

		/* Network I/O happens OUTSIDE the lock: a slow/unreachable
		 * server must never block a concurrent employees_is_authorized()
		 * call from the RFID scan thread. */
		ret = http_get(SYNC_HOST, SYNC_PORT, path, http_buf, sizeof(http_buf));
		if (ret < 0) {
			LOG_ERR("Access sync HTTP GET failed: %d", ret);
			return ret;
		}
		if ((size_t)ret >= sizeof(http_buf)) {
			LOG_ERR("Response truncated (increase http_buf), aborting merge");
			return -ENOMEM;
		}
		http_buf[ret] = '\0';

		struct sync_response resp = {0};
		int64_t parsed_mask = json_obj_parse((char *)http_buf, (size_t)ret,
						      response_descr, ARRAY_SIZE(response_descr),
						      &resp);
		if (parsed_mask < 0) {
			LOG_ERR("Failed to parse sync response: %lld", (long long)parsed_mask);
			return -EINVAL;
		}

		LOG_INF("Server returned %zu change(s)", resp.changes_len);

		k_mutex_lock(&whitelist_mutex, K_FOREVER);
		for (size_t i = 0; i < resp.changes_len; i++) {
			whitelist_apply_change_locked(&resp.changes[i]);
		}
		if (resp.changes_len > 0) {
			ret = whitelist_save_locked();
		}
		k_mutex_unlock(&whitelist_mutex);

		if (resp.changes_len > 0 && ret < 0) {
			LOG_ERR("Failed to persist whitelist: %d", ret);
			return ret;
		}

		storage_save_u64(LAST_SYNC_FILE, (uint64_t)resp.server_time);
		last_sync = (uint64_t)resp.server_time;

		if (resp.changes_len < MAX_CHANGES) {
			break; /* caught up */
		}
		LOG_INF("Batch was full (%d changes), fetching more immediately", MAX_CHANGES);
	}

	return 0;
}

struct periodic_sync_args {
	uint32_t interval_sec;
};

static void periodic_sync_thread(void *p1, void *p2, void *p3)
{
	uint32_t interval_sec = (uint32_t)(uintptr_t)p1;
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		int ret = employees_sync();

		if (ret < 0) {
			LOG_WRN("Periodic access sync failed: %d (will retry in %us)",
				ret, interval_sec);
		} else {
			LOG_INF("Periodic access sync OK, next in %us", interval_sec);
		}
		k_sleep(K_SECONDS(interval_sec));
	}
}

#define SYNC_THREAD_STACK_SIZE 4096
#define SYNC_THREAD_PRIORITY   7

K_THREAD_STACK_DEFINE(sync_thread_stack, SYNC_THREAD_STACK_SIZE);
static struct k_thread sync_thread_data;

void employees_sync_start_periodic(uint32_t interval_sec)
{
	k_thread_create(&sync_thread_data, sync_thread_stack,
			 K_THREAD_STACK_SIZEOF(sync_thread_stack),
			 periodic_sync_thread,
			 (void *)(uintptr_t)interval_sec, NULL, NULL,
			 SYNC_THREAD_PRIORITY, 0, K_NO_WAIT);
	LOG_INF("Started periodic access sync thread (every %us)", interval_sec);
}

bool employees_is_authorized(const char *rfid_uid)
{
	bool found;

	k_mutex_lock(&whitelist_mutex, K_FOREVER);
	if (!whitelist_loaded) {
		whitelist_load_locked();
	}
	found = whitelist_find_locked(rfid_uid) >= 0;
	k_mutex_unlock(&whitelist_mutex);

	return found;
}