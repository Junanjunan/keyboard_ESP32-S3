#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "change_mode_interrupt.h"


QueueHandle_t gpio_evt_queue = NULL;


/**
 * @brief   Set up GPIOs for mode selection
 * @param   mode: Pointer to the mode variable
 * @return  None
 * @note    Three pins are used for mode selection: USB, BLE, and Wireless
 * **/
void setup_mode_gpio(connection_mode_t *mode) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Interrupt on falling edge
        .mode = GPIO_MODE_INPUT,         // Set as input mode
        .pin_bit_mask = (1ULL << GPIO_USB_MODE) | (1ULL << GPIO_BLE_MODE) | (1ULL << GPIO_WIRELESS_MODE),
        .pull_up_en = 1,                 // Enable pull-up resistors
        .pull_down_en = 0                // Disable pull-down resistors
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);  // Install ISR service with default configuration
    gpio_isr_handler_add(GPIO_USB_MODE, gpio_isr_handler, (void*) GPIO_USB_MODE);
    gpio_isr_handler_add(GPIO_BLE_MODE, gpio_isr_handler, (void*) GPIO_BLE_MODE);
    gpio_isr_handler_add(GPIO_WIRELESS_MODE, gpio_isr_handler, (void*) GPIO_WIRELESS_MODE);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_evt_queue == NULL) {
        ESP_LOGE("GPIO_TASK", "Failed to create the queue");
        // Optionally, handle error such as halting or rebooting
        abort();
    }

    xTaskCreate(gpio_task, "gpio_task", 1024 * 4, (void*)mode, 10, NULL);  // Start task to handle GPIO events
}