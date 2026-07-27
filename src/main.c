#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "wifi.h"
#include "http_client.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define API_HOST "jsonplaceholder.typicode.com"
#define API_PORT "80"
#define API_PATH "/todos/1"

static uint8_t response_buffer[2048];

int main(void)
{
    int ret;

    LOG_INF("ESP32 IoT Application Starting...");

    /* 1. Connect to WiFi */
    ret = wifi_init_and_connect();
    if (ret < 0) {
        LOG_ERR("Failed to connect to network");
        return ret;
    }

    k_sleep(K_MSEC(500));

    /* 2. Fetch data from the API */
    LOG_INF("Fetching data from API...");
    ret = http_get(API_HOST, API_PORT, API_PATH, 
                   response_buffer, sizeof(response_buffer));
    
    if (ret < 0) {
        LOG_ERR("Failed to fetch data");
        return ret;
    }

    /* 3. Process the data */
    LOG_INF("JSON Payload:");
    LOG_INF("%.200s", response_buffer);

    LOG_INF("Application finished.");
    return 0;
}