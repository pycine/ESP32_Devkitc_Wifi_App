#ifndef RFID_H_
#define RFID_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <stdbool.h>

#define RFID_UID_MAX_LEN 10

typedef struct {
    uint8_t uidByte[RFID_UID_MAX_LEN];
    uint8_t size;
    uint8_t sak;
} rfid_uid_t;

/**
 * @brief Initialize the MFRC522 RFID reader over SPI.
 * @return 0 on success, negative error code on failure.
 */
int rfid_init(void);

/**
 * @brief Check if a new RFID card/tag is present.
 * @return true if a card is detected, false otherwise.
 */
bool rfid_is_new_card_present(void);

/**
 * @brief Read the UID from the detected card.
 * @param uid Pointer to an rfid_uid_t struct to populate.
 * @return true if reading succeeded, false otherwise.
 */
bool rfid_read_card_serial(rfid_uid_t *uid);

#endif /* RFID_H_ */