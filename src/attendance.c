#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#include "attendance.h"
#include "http_client.h"
#include "server_config.h"

LOG_MODULE_REGISTER(attendance, LOG_LEVEL_INF);

#define EVENT_HOST APP_SERVER_HOST
#define EVENT_PORT APP_SERVER_PORT
#define EVENT_PATH "/events"

#define MAX_UID_LEN 24
#define EVENT_QUEUE_DEPTH 16

struct attendance_event {
	char uid[MAX_UID_LEN];
	bool authorized;
};

K_MSGQ_DEFINE(event_msgq, sizeof(struct attendance_event), EVENT_QUEUE_DEPTH, 4);

static void post_event(const struct attendance_event *evt)
{
	static uint8_t resp_buf[512];
	char body[96];

	int n = snprintf(body, sizeof(body),
			  "{\"rfid_uid\":\"%s\",\"authorized\":%s}",
			  evt->uid, evt->authorized ? "true" : "false");
	if (n < 0 || (size_t)n >= sizeof(body)) {
		LOG_ERR("Event body build failed for UID %s", evt->uid);
		return;
	}

	int ret = http_post(EVENT_HOST, EVENT_PORT, EVENT_PATH,
			     "application/json", body,
			     resp_buf, sizeof(resp_buf));
	if (ret < 0) {
		LOG_WRN("Failed to report event for %s (%s): %d — dropped, not retried",
			evt->uid, evt->authorized ? "authorized" : "denied", ret);
	} else {
		LOG_INF("Reported event: %s (%s)",
			evt->uid, evt->authorized ? "authorized" : "denied");
	}
}

static void reporter_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct attendance_event evt;

	while (1) {
		/* Blocks here until the scan loop enqueues something —
		 * costs nothing while idle. */
		k_msgq_get(&event_msgq, &evt, K_FOREVER);
		post_event(&evt);
	}
}

#define REPORTER_THREAD_STACK_SIZE 4096
#define REPORTER_THREAD_PRIORITY   7

K_THREAD_STACK_DEFINE(reporter_thread_stack, REPORTER_THREAD_STACK_SIZE);
static struct k_thread reporter_thread_data;

void attendance_start_reporting(void)
{
	k_thread_create(&reporter_thread_data, reporter_thread_stack,
			 K_THREAD_STACK_SIZEOF(reporter_thread_stack),
			 reporter_thread, NULL, NULL, NULL,
			 REPORTER_THREAD_PRIORITY, 0, K_NO_WAIT);
	LOG_INF("Started attendance reporting thread");
}

void attendance_report_event(const char *rfid_uid, bool authorized)
{
	struct attendance_event evt;

	strncpy(evt.uid, rfid_uid, MAX_UID_LEN - 1);
	evt.uid[MAX_UID_LEN - 1] = '\0';
	evt.authorized = authorized;

	/* K_NO_WAIT: never block the scan loop. If the queue's full (server
	 * unreachable for a while), drop this event rather than stall. */
	if (k_msgq_put(&event_msgq, &evt, K_NO_WAIT) != 0) {
		LOG_WRN("Event queue full, dropping event for %s", rfid_uid);
	}
}
