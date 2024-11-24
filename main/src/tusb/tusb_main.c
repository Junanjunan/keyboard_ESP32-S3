/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "driver/gpio.h"
#include "hid_custom.h"
#include "esp_now_main.h"
#include "btn_progress.h"
#include "descriptors.h"

#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "example";


/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)
#define TUD_CONSUMER_CONTROL    3

typedef struct {
    TaskHandle_t task_handle;
    QueueHandle_t hid_queue;
} tinyusb_hid_t;

static tinyusb_hid_t *s_tinyusb_hid = NULL;


/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard HID device,
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_FULL_KEY_KEYBOARD(HID_REPORT_ID(REPORT_ID_FULL_KEY_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(TUD_CONSUMER_CONTROL)),
};


/**
 * @brief String descriptor
 */
const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "TinyUSB Device",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "Example HID interface",  // 4: HID
};


/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};


/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}


// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}


// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}


/********* Application ***************/

static void tusb_device_task(void *arg)
{
    while (1) {
        tud_task();
    }
}

void tinyusb_hid_keyboard_report(hid_nkey_report_t report)
{
    static bool use_full_key = false;
    // Remote wakeup
    if (tud_suspended()) {
        // Wake up host if we are in suspend mode
        // and REMOTE_WAKEUP feature is enabled by host
        tud_remote_wakeup();
    } else {
        switch (report.report_id) {
        case REPORT_ID_FULL_KEY_KEYBOARD:
            use_full_key = true;
            break;
        case REPORT_ID_KEYBOARD: {
            if (use_full_key) {
                hid_nkey_report_t _report = {0};
                _report.report_id = REPORT_ID_FULL_KEY_KEYBOARD;
                xQueueSend(s_tinyusb_hid->hid_queue, &_report, 0);
                use_full_key = false;
            }
            break;
        }
        default:
            break;
        }

        xQueueSend(s_tinyusb_hid->hid_queue, &report, 0);
    }
}

// tinyusb_hid_task function to process the HID reports
static void tinyusb_hid_task(void *arg)
{
    (void) arg;
    hid_nkey_report_t report;
    while (1) {
        if (xQueueReceive(s_tinyusb_hid->hid_queue, &report, portMAX_DELAY)) {
            // Remote wakeup
            if (tud_suspended()) {
                // Wake up host if we are in suspend mode
                // and REMOTE_WAKEUP feature is enabled by host
                tud_remote_wakeup();
                xQueueReset(s_tinyusb_hid->hid_queue);
            } else {
                if (report.report_id == REPORT_ID_KEYBOARD) {
                    tud_hid_n_report(0, REPORT_ID_KEYBOARD, &report.keyboard_report, sizeof(report.keyboard_report));
                } else if (report.report_id == REPORT_ID_FULL_KEY_KEYBOARD) {
                    tud_hid_n_report(0, REPORT_ID_FULL_KEY_KEYBOARD, &report.keyboard_full_key_report, sizeof(report.keyboard_full_key_report));
                } else if (report.report_id == REPORT_ID_CONSUMER) {
                    tud_hid_n_report(0, REPORT_ID_CONSUMER, &report.consumer_report, sizeof(report.consumer_report));
                } else {
                    // Unknown report
                    continue;
                }
                // Wait until report is sent
                if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10))) {
                    ESP_LOGW(TAG, "Report not sent");
                }
            }
        }
    }
}

void tusb_main(void)
{
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    ESP_LOGI(TAG, "USB initialization");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };

    s_tinyusb_hid = calloc(1, sizeof(tinyusb_hid_t));

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    s_tinyusb_hid->hid_queue = xQueueCreate(10, sizeof(hid_nkey_report_t));   // Adjust queue length and item size as per your requirement
    xTaskCreate(tusb_device_task, "TinyUSB", 4096, NULL, 5, NULL);
    xTaskCreate(tinyusb_hid_task, "tinyusb_hid_task", 4096, NULL, 9, &s_tinyusb_hid->task_handle);
    xTaskNotifyGive(s_tinyusb_hid->task_handle);
}
