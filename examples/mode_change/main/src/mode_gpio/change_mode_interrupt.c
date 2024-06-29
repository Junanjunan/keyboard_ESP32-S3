#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_attr.h"

#include "change_mode_interrupt.h"
#include "ble_main.h"
#include "tusb_main.h"
#include "esp_now_main.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_task_wdt.h"
#include "hid_custom.h"


void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}


// Global variable to store the current mode
connection_mode_t current_mode;


void save_mode(connection_mode_t mode) {
    nvs_flash_init();
    nvs_handle_t nvs_mode_handle;
    nvs_open("storage", NVS_READWRITE, &nvs_mode_handle);

    int32_t mode_value = (int32_t) mode;
    nvs_set_i32(nvs_mode_handle, "mode", mode_value);

    nvs_commit(nvs_mode_handle);
    nvs_close(nvs_mode_handle);
    ESP_LOGI(__func__, "Well saved and saved mode is %d", mode);
}


void gpio_task(void* arg) {
    int32_t mode_value = 0;
    nvs_handle_t nvs_mode_handle;
    nvs_flash_init();
    nvs_open("storage", NVS_READONLY, &nvs_mode_handle);
    nvs_get_i32(nvs_mode_handle, "mode", &mode_value);
    nvs_close(nvs_mode_handle);
    connection_mode_t saved_mode = (connection_mode_t) mode_value;
    bool while_break = false;

    if (saved_mode == 0) {
        saved_mode = MODE_BLE;
    }

    // Add the current task to the watchdog's monitored task list
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    keyboard_task();
    while (1) {
        if (current_mode != saved_mode) {
            ESP_LOGI(__func__, "current_mode != saved_mode");
            current_mode = saved_mode;
            switch (saved_mode) {
                case MODE_USB:
                    tusb_main();
                    save_mode(MODE_USB);
                    break;
                case MODE_BLE:
                    ble_main();
                    save_mode(MODE_BLE);
                    break;
                case MODE_WIRELESS:
                    // esp_now_main();
                    break;
                default:
                    while_break = true;
                    break;
            }
            if (while_break) {
                break;
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);

        // Reset the watchdog timer periodically
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }

    connection_mode_t *mode = (connection_mode_t*)arg;
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            switch (io_num) {
                case GPIO_USB_MODE:
                    *mode = MODE_USB;
                    break;
                case GPIO_BLE_MODE:
                    *mode = MODE_BLE;
                    break;
                case GPIO_WIRELESS_MODE:
                    *mode = MODE_WIRELESS;
                    break;
                default:
                    ESP_LOGE("GPIO_TASK", "Unhandled GPIO number received");
                    break;
            }
            if (*mode != current_mode) {
                current_mode = *mode;
                ESP_LOGI(__func__, "Changed mode is %d", current_mode);
                if (current_mode == MODE_USB) {
                    // Do something when USB mode is selected
                    ESP_LOGI(__func__, "hhh %d", current_mode);
                    tusb_main();
                    save_mode(MODE_USB);
                } else if (current_mode == MODE_BLE) {
                    ble_main();
                    save_mode(MODE_BLE);
                } else if (current_mode == MODE_WIRELESS) {
                    // esp_now_main();
                }
            }
        }
    }
}