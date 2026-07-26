#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/socket.h>

#include <errno.h>

LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_INF);

#define HTTP_HOST "jsonplaceholder.typicode.com"
#define HTTP_PORT "80"
#define HTTP_PATH "/todos/1"

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(ip_obtained_sem, 0, 1);

static uint8_t recv_buf[512];

static int response_cb(struct http_response *rsp,
                       enum http_final_call final_data,
                       void *user_data)
{
    if (final_data == HTTP_DATA_MORE) {
        LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
    } else if (final_data == HTTP_DATA_FINAL) {
        LOG_INF("HTTP Status: %s", rsp->http_status);
        LOG_INF("Response body (%zd bytes):", rsp->data_len);
        LOG_INF("%.*s", (int)rsp->data_len, rsp->recv_buf);
    }

    return 0;
}

static int http_get_request(void)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sock;
    int ret;

    LOG_INF("Resolving %s...", HTTP_HOST);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(HTTP_HOST, HTTP_PORT, &hints, &res);
    if (ret != 0) {
        LOG_ERR("DNS lookup failed: %d", ret);
        return ret;
    }

    LOG_INF("DNS resolved, creating socket...");

    sock = socket(res->ai_family, res->ai_socktype, IPPROTO_TCP);
    if (sock < 0) {
        LOG_ERR("Socket creation failed: %d", errno);
        freeaddrinfo(res);
        return -errno;
    }

    LOG_INF("Connecting to %s:%s...", HTTP_HOST, HTTP_PORT);

    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (ret < 0) {
        LOG_ERR("Connect failed: %d", errno);
        close(sock);
        return -errno;
    }

    LOG_INF("Connected, sending HTTP GET %s...", HTTP_PATH);

    struct http_request req = {
        .method = HTTP_GET,
        .url = HTTP_PATH,
        .host = HTTP_HOST,
        .protocol = "HTTP/1.1",
        .response = response_cb,
        .recv_buf = recv_buf,
        .recv_buf_len = sizeof(recv_buf),
    };

    ret = http_client_req(sock, &req, 5 * MSEC_PER_SEC, NULL);
    if (ret < 0) {
        LOG_ERR("HTTP request failed: %d", ret);
    } else {
        LOG_INF("HTTP request completed (%d bytes sent)", ret);
    }

    close(sock);
    return ret;
}

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

int main(void)
{
    int ret;

    LOG_INF("ESP32 WiFi + HTTP Client Example");

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

    LOG_INF("Waiting for IP address...");
    ret = k_sem_take(&ip_obtained_sem, K_SECONDS(30));
    if (ret < 0) {
        LOG_ERR("Timeout waiting for IP address");
        return ret;
    }

    k_sleep(K_MSEC(500));

    ret = http_get_request();
    if (ret < 0) {
        LOG_ERR("HTTP GET failed: %d", ret);
    }

    return 0;
}