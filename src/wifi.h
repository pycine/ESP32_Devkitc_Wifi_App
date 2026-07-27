#ifndef WIFI_H
#define WIFI_H

/**
 * @brief Initialize WiFi drivers, connect to the configured network,
 *        and block until an IPv4 address is obtained via DHCP.
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_init_and_connect(void);

#endif /* WIFI_H */