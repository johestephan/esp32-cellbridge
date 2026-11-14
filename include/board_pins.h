#pragma once

/**
 * Hardware pin definitions for LilyGO ESP32 Cellular Boards
 *
 * Default: LILYGO T-A7670E R2 (ESP32-WROVER-E, 4MB Flash, 8MB PSRAM)
 * Optional: LILYGO T-A7608X ESP32 (-DBOARD_LILYGO_T_A7608X)
 *
 * IMPORTANT HARDWARE NOTES (from LilyGO datasheets):
 * 1. BOARD_POWERON_PIN (GPIO12) MUST be driven HIGH at boot, otherwise the
 *    modem/SD power rail stays off and battery mode can reset.
 * 2. Do not connect external wires to GPIO4 (PWRKEY), GPIO26, GPIO27, or GPIO5.
 * 3. Remove SD card before flashing if GPIO2 interferes with boot strapping.
 * 4. Insert the Nano SIM card BEFORE powering on the board.
 */

#if defined(BOARD_LILYGO_T_A7608X)
    #define BOARD_NAME              "LilyGO T-A7608X ESP32"
    #define MODEM_MODEL_HINT        "A7608X"
    #define BOARD_POWERON_PIN       12
    #define BOARD_PWRKEY_PIN        4
    #define MODEM_RESET_PIN         5
    #define MODEM_RESET_LEVEL       HIGH
    #define MODEM_TX_PIN            27  // ESP32 TX -> Modem RX
    #define MODEM_RX_PIN            26  // ESP32 RX -> Modem TX
    #define MODEM_DTR_PIN           25
    #define MODEM_RING_PIN          33
    #define BOARD_BAT_ADC_PIN       35
    #define BOARD_BOOT_BTN_PIN      0
    #define SD_SCK_PIN              14
    #define SD_MISO_PIN             2
    #define SD_MOSI_PIN             15
    #define SD_CS_PIN               13
    #define MODEM_HAS_AUDIO         1
    #define MODEM_HAS_GPS           1
#else
    // Default: LILYGO T-A7670E R2 (without GNSS)
    #define BOARD_NAME              "LilyGO T-A7670E R2"
    #define MODEM_MODEL_HINT        "A7670E"
    #define BOARD_POWERON_PIN       12
    #define BOARD_PWRKEY_PIN        4
    // Note: T-A7670E R2 pinout does not expose a dedicated modem RST pin (GPIO5 is VSPI_CS)
    #define MODEM_TX_PIN            26  // ESP32 TX -> Modem RX
    #define MODEM_RX_PIN            27  // ESP32 RX -> Modem TX
    #define MODEM_DTR_PIN           25
    #define MODEM_RING_PIN          33
    #define BOARD_BAT_ADC_PIN       35
    #define BOARD_BOOT_BTN_PIN      0
    #define SD_SCK_PIN              14
    #define SD_MISO_PIN             2
    #define SD_MOSI_PIN             15
    #define SD_CS_PIN               13
    #define MODEM_HAS_AUDIO         0
    #define MODEM_HAS_GPS           0
#endif

#define MODEM_BAUDRATE              115200
#define SerialAT                    Serial1
