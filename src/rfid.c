#include "rfid.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rfid, LOG_LEVEL_INF);

/* MFRC522 Registers */
#define CommandReg      0x01
#define ComIEnReg       0x02
#define DivIEnReg       0x03
#define ComIrqReg       0x04
#define DivIrqReg       0x05
#define ErrorReg        0x06
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxASKReg        0x15
#define ModeReg         0x11
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define VersionReg      0x37

/* MFRC522 Commands */
#define PCD_Idle        0x00
#define PCD_Transceive  0x0C
#define PCD_SoftReset   0x0F

/* PICC Commands */
#define PICC_CMD_REQA   0x26
#define PICC_CMD_SEL_CL1 0x93
/* SPI Spec (Fixed deprecation warning by removing delay param) */
static const struct spi_dt_spec rfid_spi = SPI_DT_SPEC_GET(
    DT_NODELABEL(mfrc522),
    SPI_WORD_SET(8) | SPI_TRANSFER_MSB
);

/* Reset Pin Spec */
static const struct gpio_dt_spec rst_gpio = GPIO_DT_SPEC_GET(
    DT_PATH(zephyr_user), rfid_reset_gpios
);
/* Low-level Register Read/Write Functions */
static void rfid_write_reg(uint8_t reg, uint8_t value) {
    uint8_t tx_buf[2] = { (reg << 1) & 0x7E, value };
    struct spi_buf buf = { .buf = tx_buf, .len = 2 };
    struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

    spi_write_dt(&rfid_spi, &tx);
}

static uint8_t rfid_read_reg(uint8_t reg) {
    /* Register address formula for MFRC522: ((reg << 1) & 0x7E) | 0x80 */
    uint8_t addr = ((reg << 1) & 0x7E) | 0x80; 
    uint8_t tx_buf[2] = { addr, 0x00 };
    uint8_t rx_buf[2] = { 0, 0 };

    struct spi_buf tx_b = { .buf = tx_buf, .len = 2 };
    struct spi_buf rx_b = { .buf = rx_buf, .len = 2 };
    struct spi_buf_set tx = { .buffers = &tx_b, .count = 1 };
    struct spi_buf_set rx = { .buffers = &rx_b, .count = 1 };

    int err = spi_transceive_dt(&rfid_spi, &tx, &rx);
    if (err) {
        LOG_ERR("SPI Transceive Failed: %d", err);
        return 0;
    }

    /* Index 1 holds the data returned while sending the dummy byte */
    return rx_buf[1]; 
}
static void antenna_on(void) {
    uint8_t value = rfid_read_reg(TxControlReg);
    if ((value & 0x03) != 0x03) {
        rfid_write_reg(TxControlReg, value | 0x03);
    }
}

int rfid_init(void) {
    if (!spi_is_ready_dt(&rfid_spi)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    if (rst_gpio.port && device_is_ready(rst_gpio.port)) {
        /* Configure as output */
        gpio_pin_configure_dt(&rst_gpio, GPIO_OUTPUT_ACTIVE);
        
        /* Hardware Reset Sequence */
        gpio_pin_set_dt(&rst_gpio, 1); /* Assert reset (LOW) */
        k_msleep(50);
        gpio_pin_set_dt(&rst_gpio, 0); /* Release reset (HIGH - 3.3V) */
        k_msleep(50);
    }

    /* Soft Reset */
    rfid_write_reg(CommandReg, PCD_SoftReset);
    k_msleep(50);

    /* Timer setup & Antenna configuration */
    rfid_write_reg(TModeReg, 0x80);
    rfid_write_reg(TPrescalerReg, 0xA9);
    rfid_write_reg(TReloadRegH, 0x03);
    rfid_write_reg(TReloadRegL, 0xE8);
    rfid_write_reg(TxASKReg, 0x40);
    rfid_write_reg(ModeReg, 0x3D);

    antenna_on();

    uint8_t ver = rfid_read_reg(VersionReg);
    LOG_INF("MFRC522 Version: 0x%02X", ver);

    return (ver == 0x00 || ver == 0xFF) ? -EIO : 0;
}

static int communicate_picc(uint8_t command, uint8_t wait_irq, uint8_t *send_data, 
                             uint8_t send_len, uint8_t *back_data, uint8_t *back_len) {
    uint8_t irq_en = 0x77;
    uint8_t wait_irq_mask = wait_irq;

    rfid_write_reg(ComIEnReg, irq_en | 0x80);
    rfid_write_reg(ComIrqReg, 0x7F);
    rfid_write_reg(FIFOLevelReg, 0x80);
    rfid_write_reg(CommandReg, PCD_Idle);

    for (uint8_t i = 0; i < send_len; i++) {
        rfid_write_reg(FIFODataReg, send_data[i]);
    }

    rfid_write_reg(CommandReg, command);
    if (command == PCD_Transceive) {
        rfid_write_reg(BitFramingReg, rfid_read_reg(BitFramingReg) | 0x80);
    }

    uint16_t i = 2000;
    uint8_t n;
    do {
        n = rfid_read_reg(ComIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & wait_irq_mask));

    rfid_write_reg(BitFramingReg, rfid_read_reg(BitFramingReg) & (~0x80));

    if (i == 0) {
        return -ETIMEDOUT;
    }

    if (rfid_read_reg(ErrorReg) & 0x1B) {
        return -EIO;
    }

    if (back_data && back_len) {
        uint8_t len = rfid_read_reg(FIFOLevelReg);
        if (len > *back_len) len = *back_len;
        *back_len = len;

        for (uint8_t j = 0; j < len; j++) {
            back_data[j] = rfid_read_reg(FIFODataReg);
        }
    }

    return 0;
}

bool rfid_is_new_card_present(void) {
    uint8_t req_mode = PICC_CMD_REQA;
    uint8_t back_data[16];
    uint8_t back_len = sizeof(back_data);

    rfid_write_reg(BitFramingReg, 0x07);
    int status = communicate_picc(PCD_Transceive, 0x30, &req_mode, 1, back_data, &back_len);

    return (status == 0 && back_len == 2);
}

bool rfid_read_card_serial(rfid_uid_t *uid) {
    uint8_t cmd_buffer[2] = { PICC_CMD_SEL_CL1, 0x20 };
    uint8_t back_data[16];
    uint8_t back_len = sizeof(back_data);

    rfid_write_reg(BitFramingReg, 0x00);
    int status = communicate_picc(PCD_Transceive, 0x30, cmd_buffer, 2, back_data, &back_len);

    if (status == 0 && back_len >= 4) {
        uid->size = 4;
        for (int i = 0; i < 4; i++) {
            uid->uidByte[i] = back_data[i];
        }
        return true;
    }

    return false;
}