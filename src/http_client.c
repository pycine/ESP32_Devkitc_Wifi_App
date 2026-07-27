#include "http_client.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/socket.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(http_client, LOG_LEVEL_INF);

static int response_cb(struct http_response *rsp,
                       enum http_final_call final_data,
                       void *user_data)
{
    if (final_data == HTTP_DATA_FINAL) {
        if (rsp->data_len > 0) {
            uint8_t *dest = (uint8_t *)user_data;
            
            uint8_t *body_start = strstr((const char *)rsp->recv_buf, "\r\n\r\n");
            
            if (body_start != NULL) {
                body_start += 4;
                
                size_t header_len = body_start - rsp->recv_buf;
                size_t body_len = rsp->data_len - header_len;
                
                memcpy(dest, body_start, body_len);
                dest[body_len] = '\0';
                
                LOG_INF("Extracted %zd bytes of JSON", body_len);
            } else {
                memcpy(dest, rsp->recv_buf, rsp->data_len);
                dest[rsp->data_len] = '\0';
            }
        }
        LOG_INF("HTTP transfer complete");
    }
    return 0;
}

int http_get(const char *host, const char *port, const char *path,
             uint8_t *resp_buf, size_t resp_buf_len)
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

    LOG_INF("Connecting to %s:%s...", host, port);

    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (ret < 0) {
        LOG_ERR("Connect failed: %d", errno);
        close(sock);
        return -errno;
    }

    LOG_INF("Sending HTTP GET %s...", path);

   
    struct http_request req = {
        .method = HTTP_GET,
        .url = path,
        .host = host,
        .protocol = "HTTP/1.1",
        .response = response_cb,
        .recv_buf = resp_buf,
        .recv_buf_len = resp_buf_len,
    };

    ret = http_client_req(sock, &req, 5000, resp_buf);
        if (ret < 0) {
        LOG_ERR("HTTP request failed: %d", ret);
    } else {
        LOG_INF("HTTP request completed successfully");
    }

    close(sock);
    return ret;
}
int http_post(const char *host, const char *port, const char *path,
              const char *content_type, const char *body,
              uint8_t *resp_buf, size_t resp_buf_len)
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

    LOG_INF("Connecting to %s:%s...", host, port);

    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (ret < 0) {
        LOG_ERR("Connect failed: %d", errno);
        close(sock);
        return -errno;
    }

    char tx_buf[1024];
    int body_len = strlen(body);

    snprintf(tx_buf, sizeof(tx_buf),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             path, host, content_type, body_len, body);

    LOG_INF("Sending HTTP POST %s...", path);

    ret = send(sock, tx_buf, strlen(tx_buf), 0);
    if (ret < 0) {
        LOG_ERR("Failed to send POST data: %d", errno);
        close(sock);
        return -errno;
    }

    int total_read = 0;
    int bytes_read;
    
    while (total_read < (int)resp_buf_len - 1) {
        bytes_read = recv(sock, &resp_buf[total_read], 
                          resp_buf_len - total_read - 1, 0);
        if (bytes_read <= 0) {
            break; 
        }
        total_read += bytes_read;
    }
    resp_buf[total_read] = '\0'; /* Null-terminate the raw response */
    
    close(sock);

    char *body_start = strstr((char *)resp_buf, "\r\n\r\n");
    if (body_start != NULL) {
        body_start += 4;
        size_t header_len = body_start - (char *)resp_buf;
        size_t json_len = total_read - header_len;
        
        memmove(resp_buf, body_start, json_len);
        resp_buf[json_len] = '\0';
        
        LOG_INF("POST completed successfully");
    } else {
        LOG_WRN("Could not parse HTTP response");
    }

    return 0;
}