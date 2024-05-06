#include "driver/gpio.h"

#define GPIO_USB_MODE           4
#define GPIO_BLE_MODE           5
#define GPIO_WIRELESS_MODE      6


/**
 * @brief   Set up GPIOs for mode selection
 * @param   None
 * @return  None
 * @note    Three pins are used for mode selection: USB, BLE, and Wireless
 * **/
void setup_mode_gpio() {
    // Set GPIOs for USB, BLE, and Wireless mode selection
    esp_rom_gpio_pad_select_gpio(GPIO_USB_MODE);
    esp_rom_gpio_pad_select_gpio(GPIO_BLE_MODE);
    esp_rom_gpio_pad_select_gpio(GPIO_WIRELESS_MODE);

    // Configure GPIOs as input
    gpio_set_direction(GPIO_USB_MODE, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_BLE_MODE, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_WIRELESS_MODE, GPIO_MODE_INPUT);

    // Enable pull-up resistors
    gpio_set_pull_mode(GPIO_USB_MODE, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_BLE_MODE, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_WIRELESS_MODE, GPIO_PULLUP_ONLY);
}


typedef enum {
    MODE_USB,
    MODE_BLE,
    MODE_WIRELESS
} connection_mode_t;

/**
 * @brief   Detect the current mode based on the GPIOs
 * @param   None
 * @return  The current connection mode
 * @note    Mode is determined by the GPIOs: USB, BLE, and Wireless
 * **/
connection_mode_t detect_current_mode() {
    if (0 == gpio_get_level(GPIO_USB_MODE)) {
        return MODE_USB;
    } else if (0 == gpio_get_level(GPIO_BLE_MODE)) {
        return MODE_BLE;
    } else if (0 == gpio_get_level(GPIO_WIRELESS_MODE)) {
        return MODE_WIRELESS;
    }
    return MODE_USB; // Default mode
}
