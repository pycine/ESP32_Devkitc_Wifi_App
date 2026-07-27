#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Perform an HTTP GET request.
 * 
 * @param host    The hostname (e.g., "jsonplaceholder.typicode.com")
 * @param port    The port string (e.g., "80" or "443")
 * @param path    The URL path (e.g., "/todos/1")
 * @param resp_buf Buffer to store the HTTP response body
 * @param resp_buf_len Size of the response buffer
 * 
 * @return 0 on success, negative error code on failure.
 */
int http_get(const char *host, const char *port, const char *path,
             uint8_t *resp_buf, size_t resp_buf_len);

/* New POST function */
int http_post(const char *host, const char *port, const char *path,
              const char *content_type, const char *body,
              uint8_t *resp_buf, size_t resp_buf_len);

#endif /* HTTP_CLIENT_H */


