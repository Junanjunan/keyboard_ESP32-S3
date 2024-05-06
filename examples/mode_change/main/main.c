#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"

#include "mode_gpio.h"


void app_main() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      ret = nvs_flash_init();
    }

    setup_mode_gpio();
    connection_mode_t mode = detect_current_mode();
    ESP_LOGI("MODE", "Current mode: %d (0: USB, 1: BLE, 2: Wireless)", mode);
}
