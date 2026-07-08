~~~c
// BOARD PIN DEFINE
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43
#define SerialMon           Serial
#define SerialGPS           Serial2

#define BOARD_I2C_PORT      (0)
#define BOARD_SCL           (40)
#define BOARD_SDA           (39)

#define BOARD_SPI_MISO      (21)
#define BOARD_SPI_MOSI      (13)
#define BOARD_SPI_SCLK      (14)

#define BOARD_TOUCH_SCL     (BOARD_SCL)
#define BOARD_TOUCH_SDA     (BOARD_SDA)
#define BOARD_TOUCH_INT     (3)
#define BOARD_TOUCH_RST     (9)

#define BOARD_RTC_SCL       (BOARD_SCL)
#define BOARD_RTC_SDA       (BOARD_SDA)
#define BOARD_RTC_IRQ       (2)

#define BOARD_SD_MISO       (BOARD_SPI_MISO)
#define BOARD_SD_MOSI       (BOARD_SPI_MOSI)
#define BOARD_SD_SCLK       (BOARD_SPI_SCLK)
#define BOARD_SD_CS         (12)

#define BOARD_LORA_MISO     (BOARD_SPI_MISO)
#define BOARD_LORA_MOSI     (BOARD_SPI_MOSI)
#define BOARD_LORA_SCLK     (BOARD_SPI_SCLK)
#define BOARD_LORA_CS       (46)
#define BOARD_LORA_IRQ      (10)
#define BOARD_LORA_RST      (1)
#define BOARD_LORA_BUSY     (47)

#define BOARD_BL_EN         (11)
#define BOARD_PCA9535_INT   (38)
#define BOARD_BOOT_BTN      (0)

// ED047TC1 --- e-ink paper
#define EP_D7              (8)
#define EP_D6              (18)
#define EP_D5              (17)
#define EP_D4              (16)
#define EP_D3              (15)
#define EP_D2              (7)
#define EP_D1              (6)
#define EP_D0              (5)
#define EP_CKV             (48) /* Control Lines */
#define EP_STH             (41)
#define EP_LEH             (42)
#define EP_STV             (45)
#define EP_CKH             (4)   /* Edges */

// PCA9535
// Extend the interface and set the read/write ports via I2C.
// IO1X
#define PCA9535_IO10_EP_OE          // EP Output enable source driver
#define PCA9535_IO11_EP_MODE        // EP Output mode selection gate driver
#define PCA9535_IO12_BUTTON
#define PCA9535_IO13_TPS_PWRUP
#define PCA9535_IO14_VCOM_CTRL
#define PCA9535_IO15_TPS_WAKEUP
#define PCA9535_IO16_TPS_PWR_GOOD
#define PCA9535_IO17_TPS_INT
// IO0X
#define PCA9535_IO00
#define PCA9535_IO01
#define PCA9535_IO02
#define PCA9535_IO03
#define PCA9535_IO04
#define PCA9535_IO05
#define PCA9535_IO06
#define PCA9535_IO07

~~~