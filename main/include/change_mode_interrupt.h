#include "freertos/queue.h"
#include "esp_attr.h"
#include "mode_gpio.h"

#define GPIO_USB_MODE           4
#define GPIO_BLE_MODE           5
#define GPIO_WIRELESS_MODE      6


extern QueueHandle_t gpio_evt_queue;


extern connection_mode_t current_mode;


/**
 * @brief   ISR handler for GPIO events
 * @param   arg: Pointer to the GPIO number that triggered the interrupt, but casted to void* to use in gpio_isr_handler_add(esp_err_t gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args))
 * @return  None
 * @note    This function will send the GPIO number to the queue
 * **/
void gpio_isr_handler(void *arg);


/**
 * @brief   Task to handle GPIO events
 * @param   arg: Pointer to the mode variable, but casted to void* to use in FreeRTOS task
 * @return  None
 * @note    This task will handle GPIO events and change the mode variable accordingly
 * **/
void gpio_task(void *arg);


void save_mode(connection_mode_t mode);