#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "wifi.h"
#include "http_client.h"
#include "storage.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* API Endpoints */
#define API_HOST "jsonplaceholder.typicode.com"
#define API_PORT "80"
#define API_PATH "/todos/1"

#define WEBHOOK_HOST "webhook.site"
#define WEBHOOK_PORT "80"
#define WEBHOOK_PATH "/bac98fb5-b92e-4d75-b4ae-5c0e6beafe33"

/* File name to store our GET data in LittleFS */
#define DATA_FILE "last_api_response.txt"

/* Buffers */
static uint8_t response_buffer[2048];
static char saved_data[2048];

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
    memset(saved_data, 0, sizeof(saved_data));
    ret = storage_load(DATA_FILE, saved_data, sizeof(saved_data) - 1);
    
    if (ret > 0) {
        LOG_INF("=== DATA SURVIVED THE REBOOT ===");
        LOG_INF("%.200s", saved_data);
        LOG_INF("=================================");
    } else {
        LOG_INF("No existing data found (normal on first boot).");
    }

    /* 3. Connect to WiFi */
    ret = wifi_init_and_connect();
    if (ret < 0) {
        LOG_ERR("Failed to connect to network");
        return ret;
    }

    k_sleep(K_MSEC(500));
    

    /* 4. Fetch FRESH data from the API */
    LOG_INF("Fetching fresh data from API...");
    ret = http_get(API_HOST, API_PORT, API_PATH, 
                   response_buffer, sizeof(response_buffer));
    
    if (ret < 0) {
        LOG_ERR("Failed to fetch data");
        return ret;
    }

    LOG_INF("Fresh JSON Payload received:");
    LOG_INF("%.200s", response_buffer);

    /* 5. Save the fresh data to LittleFS for the NEXT reboot */
       /* 5. Save the fresh data to LittleFS for the NEXT reboot */
    LOG_INF("Saving fresh data to flash...");
    ret = storage_save(DATA_FILE, (const char *)response_buffer, strlen((const char *)response_buffer));
    if (ret >= 0) {
        LOG_INF("Data saved safely to LittleFS.");
    } else {
        LOG_ERR("Failed to save data to flash: %d", ret);
    }

    k_sleep(K_MSEC(500));

    /* 6. Send a POST request (e.g., sensor status) */
    const char *json_payload = "{\"temperature\":25,\"humidity\":15}";
    
    LOG_INF("Posting sensor data to Webhook...");
    ret = http_post(WEBHOOK_HOST, WEBHOOK_PORT, WEBHOOK_PATH,
                    "application/json", json_payload,
                    response_buffer, sizeof(response_buffer));
    
    if (ret < 0) {
        LOG_ERR("Failed to POST data");
    } else {
        LOG_INF("Data sent successfully!");
    }

    LOG_INF("Application finished.");
    return 0;
}