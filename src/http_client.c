#include "http_client.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/socket.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(http_client, LOG_LEVEL_INF);

/* Hard backstop so a stuck/unreachable server can never hang the device
 * forever, regardless of what the peer does. Applies to both GET and POST. */
#define HTTP_RECV_TIMEOUT_SEC 10

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sock;
    int ret;

    LOG_INF("Resolving %s...", host);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(host, port, &hints, &res);
    if (ret != 0) {
        LOG_ERR("DNS lookup failed: %d", ret);
        return ret;
    }

    sock = socket(res->ai_family, res->ai_socktype, IPPROTO_TCP);
    if (sock < 0) {
        LOG_ERR("Socket creation failed: %d", errno);
        freeaddrinfo(res);
        return -errno;
    }

    struct timeval tv = {
        .tv_sec = HTTP_RECV_TIMEOUT_SEC,
        .tv_usec = 0,
    };
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_WRN("Failed to set SO_RCVTIMEO: %d (continuing without it)", errno);
    }

    LOG_INF("Connecting to %s:%s...", host, port);

    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (ret < 0) {
        LOG_ERR("Connect failed: %d", errno);
        close(sock);
        return -errno;
    }

    return sock;
}

/* Reads until the peer closes the connection (server sends
 * "Connection: close") or resp_buf fills up, then strips the HTTP headers
 * off, leaving just the body in resp_buf, null-terminated.
 * Returns the body length, or a negative errno.
 */
static int recv_and_strip_headers(int sock, uint8_t *resp_buf, size_t resp_buf_len)
{
    int total_read = 0;
    int bytes_read;

    while (total_read < (int)resp_buf_len - 1) {
        bytes_read = recv(sock, &resp_buf[total_read], resp_buf_len - total_read - 1, 0);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_ERR("recv() timed out after %ds waiting for response",
                        HTTP_RECV_TIMEOUT_SEC);
                return -ETIMEDOUT;
            }
            LOG_ERR("recv() failed: %d", errno);
            return -errno;
        }
        if (bytes_read == 0) {
            break; /* peer closed the connection: response complete */
        }
        total_read += bytes_read;
    }

    if (total_read >= (int)resp_buf_len - 1) {
        LOG_WRN("Response filled resp_buf (%zu bytes); may be truncated", resp_buf_len);
    }
    resp_buf[total_read] = '\0';

    char *body_start = strstr((char *)resp_buf, "\r\n\r\n");
    if (body_start == NULL) {
        LOG_WRN("Could not find end of headers in response");
        return total_read; /* hand back the raw buffer as-is */
    }
    body_start += 4;

    size_t header_len = body_start - (char *)resp_buf;
    size_t body_len = total_read - header_len;

    /* Same buffer is both source and destination: MUST use memmove, not
     * memcpy, since the regions overlap. */
    memmove(resp_buf, body_start, body_len);
    resp_buf[body_len] = '\0';

    return (int)body_len;
}

int http_get(const char *host, const char *port, const char *path,
             uint8_t *resp_buf, size_t resp_buf_len)
{
    int sock = connect_to(host, port);
    if (sock < 0) {
        return sock;
    }

    char tx_buf[512];
    int n = snprintf(tx_buf, sizeof(tx_buf),
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      path, host);
    if (n < 0 || (size_t)n >= sizeof(tx_buf)) {
        LOG_ERR("GET request too long for tx_buf (path length?)");
        close(sock);
        return -ENOMEM;
    }

    LOG_INF("Sending HTTP GET %s...", path);

    int ret = send(sock, tx_buf, strlen(tx_buf), 0);
    if (ret < 0) {
        LOG_ERR("Failed to send GET request: %d", errno);
        close(sock);
        return -errno;
    }

    ret = recv_and_strip_headers(sock, resp_buf, resp_buf_len);
    close(sock);

    if (ret < 0) {
        LOG_ERR("HTTP GET failed: %d", ret);
    } else {
        LOG_INF("HTTP GET completed successfully (%d byte body)", ret);
    }
    return ret;
}

int http_post(const char *host, const char *port, const char *path,
              const char *content_type, const char *body,
              uint8_t *resp_buf, size_t resp_buf_len)
{
    int sock = connect_to(host, port);
    if (sock < 0) {
        return sock;
    }

    char tx_buf[1024];
    int body_len = strlen(body);

    int n = snprintf(tx_buf, sizeof(tx_buf),
                      "POST %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %d\r\n"
                      "Connection: close\r\n"
                      "\r\n"
                      "%s",
                      path, host, content_type, body_len, body);
    if (n < 0 || (size_t)n >= sizeof(tx_buf)) {
        LOG_ERR("POST request too long for tx_buf (body too large?)");
        close(sock);
        return -ENOMEM;
    }

    LOG_INF("Sending HTTP POST %s...", path);

    int ret = send(sock, tx_buf, strlen(tx_buf), 0);
    if (ret < 0) {
        LOG_ERR("Failed to send POST data: %d", errno);
        close(sock);
        return -errno;
    }

    ret = recv_and_strip_headers(sock, resp_buf, resp_buf_len);
    close(sock);

    if (ret < 0) {
        LOG_ERR("HTTP POST failed: %d", ret);
    } else {
        LOG_INF("HTTP POST completed successfully (%d byte body)", ret);
    }
    return ret;
}