#include "wifi.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <string.h>

LOG_MODULE_REGISTER(wifi, LOG_LEVEL_INF);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(ip_obtained_sem, 0, 1);

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        LOG_INF("WiFi connected");
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_INF("WiFi disconnected");
    }
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

        if (ipv4) {
            char addr_str[NET_IPV4_ADDR_LEN];

            for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
                if (ipv4->unicast[i].ipv4.is_used) {
                    net_addr_ntop(AF_INET,
                                  &ipv4->unicast[i].ipv4.address.in_addr,
                                  addr_str, sizeof(addr_str));
                    LOG_INF("IP Address: %s", addr_str);
                }
            }
        }
        k_sem_give(&ip_obtained_sem);
    }
}

static int connect_wifi(void)
{
    struct net_if *iface = net_if_get_default();
    struct wifi_connect_req_params params = {
        .ssid = CONFIG_WIFI_SSID,
        .ssid_length = strlen(CONFIG_WIFI_SSID),
        .psk = CONFIG_WIFI_PSK,
        .psk_length = strlen(CONFIG_WIFI_PSK),
        .channel = WIFI_CHANNEL_ANY,
        .band = WIFI_FREQ_BAND_2_4_GHZ,
        .security = WIFI_SECURITY_TYPE_PSK,
    };

    LOG_INF("Connecting to %s...", CONFIG_WIFI_SSID);
    return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

int wifi_init_and_connect(void)
{
    int ret;

    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);

    k_sleep(K_SECONDS(2));

    ret = connect_wifi();
    if (ret) {
        LOG_ERR("WiFi connect request failed: %d", ret);
        return ret;
    }

    /* Block until DHCP gives us an IP */
    LOG_INF("Waiting for IP address...");
    ret = k_sem_take(&ip_obtained_sem, K_SECONDS(30));
    if (ret < 0) {
        LOG_ERR("Timeout waiting for IP address");
        return ret;
    }

    return 0;
}