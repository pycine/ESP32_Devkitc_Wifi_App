#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "wifi.h"
#include "http_client.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Using HTTP (port 80) for now. Webhook.site will still receive it! */
#define WEBHOOK_HOST "webhook.site"
#define WEBHOOK_PORT "80"
#define WEBHOOK_PATH "/bac98fb5-b92e-4d75-b4ae-5c0e6beafe33"
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
    k_sleep(K_MSEC(500));
    LOG_INF("Get finished.");
    /* 2. Prepare the sensor data */
    const char *json_payload = "{\"temperature\":25,\"humidity\":15}";
    
    LOG_INF("Posting sensor data to Webhook...");

    /* 3. Send the POST request */
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