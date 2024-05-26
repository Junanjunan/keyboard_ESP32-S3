#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_event.h"
#include "esp_log.h"
#include "hid_dev.h"
#include "keyboard_button.h"
#include "change_mode_interrupt.h"
#include "esp_now.h"


static uint16_t hid_conn_id = 0;

keyboard_btn_config_t cfg = {
    .output_gpios = (int[])
    {
        18, 19
    },
    .output_gpio_num = 2,
    .input_gpios = (int[])
    {
        32, 33
    },
    .input_gpio_num = 2,
    .active_level = 1,
    .debounce_ticks = 2,
    .ticks_interval = 500,      // us
    .enable_power_save = false, // enable power save
};

keyboard_btn_handle_t kbd_handle = NULL;

uint8_t keycodes[2][2] = {
    {HID_KEY_A, HID_KEY_B},  // HID keycodes for 'a', 'b'
    {HID_KEY_C, HID_KEY_D}   // HID keycodes for 'c', 'd'
};

static uint8_t peer_mac [ESP_NOW_ETH_ALEN] = {0x24, 0x58, 0x7C, 0xCD, 0x93, 0x30};

void keyboard_cb(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data)
{
    uint8_t keycode = 0;
    uint8_t key[6] = {keycode};
    uint8_t espnow_release_key [] = "0";
    if (kbd_report.key_pressed_num == 0) {
        if (current_mode == MODE_USB)
        {
            ESP_LOGI(__func__, "Removed TUD in ESP32");
        }
        else if (current_mode == MODE_BLE)
        {
            esp_hidd_send_keyboard_value(hid_conn_id, 0, &keycode, 1);
        }
        else if (current_mode == MODE_WIRELESS)
        {
            esp_now_send(peer_mac, espnow_release_key, 32);
        }
        return;
    }

    for (int i = 0; i < kbd_report.key_pressed_num; i++) {
        keycode = keycodes[kbd_report.key_data[i].output_index][kbd_report.key_data[i].input_index];

        char temp [6];
        uint8_t converted_data [6];
        sprintf(temp, "%d", keycode);
        memcpy(converted_data, temp, sizeof(temp));

        uint8_t key[6] = {keycode};
        if (current_mode == MODE_USB)
        {
            ESP_LOGI(__func__, "Removed TUD in ESP32");
        }
        else if (current_mode == MODE_BLE)
        {
            esp_hidd_send_keyboard_value(hid_conn_id, 0, &keycode, 1);
        }
        else if (current_mode == MODE_WIRELESS)
        {
            esp_now_send(peer_mac, converted_data, 32);
        }
    }
}

keyboard_btn_cb_config_t cb_cfg = {
    .event = KBD_EVENT_PRESSED,
    .callback = keyboard_cb,
};

void keyboard_task(void) {
    keyboard_button_create(&cfg, &kbd_handle);
    keyboard_button_register_cb(kbd_handle, cb_cfg, NULL);
}