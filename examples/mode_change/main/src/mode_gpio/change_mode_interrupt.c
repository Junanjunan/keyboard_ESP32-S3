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


void gpio_task(void* arg) {
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
                } else if (current_mode == MODE_BLE) {
                    ble_main();
                } else if (current_mode == MODE_WIRELESS) {
                    // esp_now_main();
                }
            }
        }
    }
}