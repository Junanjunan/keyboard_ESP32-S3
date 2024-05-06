#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mode_gpio.h"


void app_main() {
    connection_mode_t mode = 0;
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      ret = nvs_flash_init();
    }

    setup_mode_gpio();
    while (1) {
        connection_mode_t current_mode = detect_current_mode();
        if (current_mode != mode) {
            mode = current_mode;
            ESP_LOGI("MODE", "changed mode: %d (0: USB, 1: BLE, 2: Wireless)", mode);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
