#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include "wifi.h"
#include "http_client.h"
#include "storage.h"
#include "rfid.h"
#include "employee_sync.h"
#include "attendance.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* LED from alias */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* API Endpoints */
#define API_HOST "jsonplaceholder.typicode.com"
#define API_PORT "80"
#define API_PATH "/todos/1"

#define WEBHOOK_HOST "webhook.site"
#define WEBHOOK_PORT "80"
#define WEBHOOK_PATH "/bac98fb5-b92e-4d75-b4ae-5c0e6beafe33"
#define CARD_COOLDOWN_MS 3000  /* ignore repeat reads of the same card within 3s */

/* File name to store our GET data in LittleFS */
#define DATA_FILE "last_api_response.txt"

/* Forward declaration (required before K_TIMER_DEFINE) */
void led_timer_expiry(struct k_timer *timer);

/* Static timer definition — no k_timer_init needed! */
K_TIMER_DEFINE(led_timer, led_timer_expiry, NULL);

/* Buffers */
static uint8_t response_buffer[2048];
static char saved_data[2048];

static char last_uid[32] = {0};
static int64_t last_read_time = 0;

/* Callback: turn LED off when timer expires */
void led_timer_expiry(struct k_timer *timer)
{
    gpio_pin_set_dt(&led, 0);  /* Turn off after delay */
}

int main(void)
{
    int ret;

    LOG_INF("ESP32 IoT Application Starting...");

    /* 1. Initialize LittleFS */
    ret = storage_init();
    if (ret < 0) {
        LOG_ERR("Storage initialization failed: %d", ret);
        return ret;
    }
    LOG_INF("Storage mounted successfully.");

    /* 2. Check flash for data from the LAST boot */
    ret = storage_load(DATA_FILE, saved_data, sizeof(saved_data) - 1);
    if (ret > 0) {
        LOG_INF("=== DATA SURVIVED THE REBOOT ===");
        LOG_INF("%.200s", saved_data);
        LOG_INF("=================================");
    } else {
        LOG_INF("No existing data found (normal on first boot).");
    }

    /* 3. Initialize LED */
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED GPIO not ready");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("LED config failed: %d", ret);
        return ret;
    }

    /* 4. Connect to WiFi */
    ret = wifi_init_and_connect();
    if (ret < 0) {
        LOG_ERR("Failed to connect to network");
        return ret;
    }

    k_sleep(K_MSEC(500));

    if (employees_sync() < 0) {
        LOG_WRN("Employee sync failed, using stale local cache");
    }

    if (rfid_init() != 0) {
        LOG_ERR("Failed to initialize MFRC522");
    } else {
        LOG_INF("MFRC522 RFID Reader Ready!");
    }

    employees_sync_start_periodic(15);
    attendance_start_reporting();

    rfid_uid_t uid;
    while (1) {
        if (rfid_is_new_card_present() && rfid_read_card_serial(&uid)) {
            char uid_str[32] = {0};
            char tmp[4];

            for (uint8_t i = 0; i < uid.size; i++) {
                snprintf(tmp, sizeof(tmp), "%02X", uid.uidByte[i]);
                strcat(uid_str, tmp);
            }

            int64_t now = k_uptime_get();
            bool same_card = (strcmp(uid_str, last_uid) == 0);
            bool cooldown_passed = (now - last_read_time) >= CARD_COOLDOWN_MS;

            if (!same_card || cooldown_passed) {
                bool authorized = employees_is_authorized(uid_str);

                if (authorized) {
                    LOG_INF("Access granted: %s", uid_str);
                    gpio_pin_set_dt(&led, 1);
                    k_timer_start(&led_timer, K_MSEC(1000), K_NO_WAIT);
                } else {
                    LOG_WRN("Access denied: %s", uid_str);
                }

                attendance_report_event(uid_str, authorized);

                strncpy(last_uid, uid_str, sizeof(last_uid) - 1);
                last_uid[sizeof(last_uid) - 1] = '\0';  /* Ensure null termination */
                last_read_time = now;
            }
        }
        k_msleep(250);
    }

    /* Unreachable — kept only for completeness */
    LOG_INF("Application finished.");
    return 0;
}