#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "esp_log.h"

#define GPIO_USB_MODE           4
#define GPIO_BLE_MODE           5
#define GPIO_WIRELESS_MODE      6


static QueueHandle_t gpio_evt_queue = NULL;

/**
 * @brief   ISR handler for GPIO events
 * @param   arg: Pointer to the GPIO number that triggered the interrupt, but casted to void* to use in gpio_isr_handler_add(esp_err_t gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args))
 * @return  None
 * @note    This function will send the GPIO number to the queue
 * **/
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}


typedef enum {
    MODE_USB,
    MODE_BLE,
    MODE_WIRELESS
} connection_mode_t;

// Global variable to store the current mode
connection_mode_t current_mode;

/**
 * @brief   Task to handle GPIO events
 * @param   arg: Pointer to the mode variable, but casted to void* to use in FreeRTOS task
 * @return  None
 * @note    This task will handle GPIO events and change the mode variable accordingly
 * **/
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
            }
        }
    }
}

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