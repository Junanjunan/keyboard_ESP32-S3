#include "esp_event.h"
#include "esp_log.h"
#include "hid_dev.h"
#include "keyboard_button.h"
#include "tinyusb.h"
#include "change_mode_interrupt.h"
#include "esp_now.h"
#include "esp_now_main.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "ble_main.h"
#include "esp_gap_ble_api.h"

#define TUD_CONSUMER_CONTROL    3

static uint16_t hid_conn_id = 0;

bool use_fn = false;
bool use_right_shift = false;


keyboard_btn_config_t cfg = {
    .output_gpios = (int[])
    {
        // 5, 6, 7, 8, 9, 10
        2, 4, 5, 6, 7, 8,
    },
    .output_gpio_num = 6,
    .input_gpios = (int[])
    {
        // 11, 12, 13, 14, 15, 16, 17, 18, 21, 33, 34, 35, 36, 37, 38, 47, 48
        9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 35, 36, 37, 38, 47, 48
        // GPIO 33 and 34 are not in devkitc, So I use 9, 10 for first start of column and row starts from 2, 4
        // If I need more analog pins (Now GPIO 1 is only one rest analog pin),
        // I think I have to use GPIO 39, 40, 41, 42 for JTAG pins
        // or GPIO 43, 44 for UART pins
        // If I can use 33, 34 in real chip (not dev board), I can have rest 3 analog pins.
        // In this case, I think analog pins are enough.
    },
    .input_gpio_num = 17,
    .active_level = 1,
    .debounce_ticks = 2,
    .ticks_interval = 500,      // us
    .enable_power_save = false, // enable power save
};


keyboard_btn_handle_t kbd_handle = NULL;


// make function key at the bottom of F8 line (Current: HID_KEY_GUI_RIGHT)
uint8_t keycodes[6][17] = {
    {HID_KEY_ESCAPE,                HID_KEY_NONE,               HID_KEY_F1,                 HID_KEY_F2,     HID_KEY_F3,     HID_KEY_F4,      HID_KEY_F5,    HID_KEY_F6,     HID_KEY_F7,     HID_KEY_F8,                 HID_KEY_F9,                         HID_KEY_F10,            HID_KEY_F11,            HID_KEY_F12,                    HID_KEY_PRINT_SCREEN,   HID_KEY_SCROLL_LOCK,    HID_KEY_PAUSE},
    {HID_KEY_GRAVE,                 HID_KEY_1,                  HID_KEY_2,                  HID_KEY_3,      HID_KEY_4,      HID_KEY_5,       HID_KEY_6,     HID_KEY_7,      HID_KEY_8,      HID_KEY_9,                  HID_KEY_0,                          HID_KEY_MINUS,          HID_KEY_EQUAL,          HID_KEY_BACKSPACE,              HID_KEY_INSERT,         HID_KEY_HOME,           HID_KEY_PAGE_UP},
    {HID_KEY_TAB,                   HID_KEY_Q,                  HID_KEY_W,                  HID_KEY_E,      HID_KEY_R,      HID_KEY_T,       HID_KEY_Y,     HID_KEY_U,      HID_KEY_I,      HID_KEY_O,                  HID_KEY_P,                          HID_KEY_BRACKET_LEFT,   HID_KEY_BRACKET_RIGHT,  HID_KEY_BACKSLASH,              HID_KEY_DELETE,         HID_KEY_END,            HID_KEY_PAGE_DOWN},
    {HID_KEY_CAPS_LOCK,             HID_KEY_A,                  HID_KEY_S,                  HID_KEY_D,      HID_KEY_F,      HID_KEY_G,       HID_KEY_H,     HID_KEY_J,      HID_KEY_K,      HID_KEY_L,                  HID_KEY_SEMICOLON,                  HID_KEY_APOSTROPHE,     HID_KEY_NONE,           HID_KEY_ENTER,                  HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTSHIFT,   HID_KEY_Z,                  HID_KEY_X,                  HID_KEY_C,      HID_KEY_V,      HID_KEY_B,       HID_KEY_N,     HID_KEY_M,      HID_KEY_COMMA,  HID_KEY_PERIOD,             HID_KEY_SLASH,                      HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_SHIFT_RIGHT,            HID_KEY_NONE,           HID_KEY_ARROW_UP,       HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTCTRL,    KEYBOARD_MODIFIER_LEFTGUI,  KEYBOARD_MODIFIER_LEFTALT,  HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE,    HID_KEY_SPACE, HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_NONE,               KEYBOARD_MODIFIER_RIGHTALT,         HID_KEY_NONE,    HID_KEY_APPLICATION,           KEYBOARD_MODIFIER_RIGHTCTRL,    HID_KEY_ARROW_LEFT,     HID_KEY_ARROW_DOWN,     HID_KEY_ARROW_RIGHT}
};

uint8_t fn_keycodes[6][17] = {
    {HID_KEY_ESCAPE,                HID_KEY_NONE,               HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT,    HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT,    HID_KEY_F3,     HID_KEY_F4,     HID_KEY_F5,     HID_KEY_F6,     HID_USAGE_CONSUMER_SCAN_PREVIOUS,   HID_CONSUMER_PAUSE,         HID_USAGE_CONSUMER_SCAN_NEXT,   HID_USAGE_CONSUMER_MUTE,    HID_USAGE_CONSUMER_VOLUME_DECREMENT,    HID_USAGE_CONSUMER_VOLUME_INCREMENT,    HID_KEY_PRINT_SCREEN,   HID_KEY_SCROLL_LOCK,    HID_KEY_PAUSE},
    {HID_KEY_GRAVE,                 HID_KEY_1,                  HID_KEY_2,                                  HID_KEY_3,                                  HID_KEY_4,      HID_KEY_5,      HID_KEY_6,      HID_KEY_7,      HID_KEY_8,                          HID_KEY_9,                  HID_KEY_0,                      HID_KEY_MINUS,              HID_KEY_EQUAL,                          HID_KEY_BACKSPACE,                      HID_KEY_INSERT,         HID_KEY_HOME,           HID_KEY_PAGE_UP},
    {HID_KEY_TAB,                   HID_KEY_Q,                  HID_KEY_W,                                  HID_KEY_E,                                  HID_KEY_R,      HID_KEY_T,      HID_KEY_Y,      HID_KEY_U,      HID_KEY_I,                          HID_KEY_O,                  HID_KEY_P,                      HID_KEY_BRACKET_LEFT,       HID_KEY_BRACKET_RIGHT,                  HID_KEY_BACKSLASH,                      HID_KEY_DELETE,         HID_KEY_END,            HID_KEY_PAGE_DOWN},
    {HID_KEY_CAPS_LOCK,             HID_KEY_A,                  HID_KEY_S,                                  HID_KEY_D,                                  HID_KEY_F,      HID_KEY_G,      HID_KEY_H,      HID_KEY_J,      HID_KEY_K,                          HID_KEY_L,                  HID_KEY_SEMICOLON,              HID_KEY_APOSTROPHE,         HID_KEY_NONE,                           HID_KEY_ENTER,                          HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTSHIFT,   HID_KEY_Z,                  HID_KEY_X,                                  HID_KEY_C,                                  HID_KEY_V,      HID_KEY_B,      HID_KEY_N,      HID_KEY_M,      HID_KEY_COMMA,                      HID_KEY_PERIOD,             HID_KEY_SLASH,                  HID_KEY_NONE,               HID_KEY_NONE,                           HID_KEY_SHIFT_RIGHT,                    HID_KEY_NONE,           HID_KEY_ARROW_UP,       HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTCTRL,    KEYBOARD_MODIFIER_LEFTGUI,  KEYBOARD_MODIFIER_LEFTALT,                  HID_KEY_NONE,                               HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_SPACE,  HID_KEY_NONE,   HID_KEY_NONE,                       HID_KEY_NONE,               KEYBOARD_MODIFIER_RIGHTALT,     HID_KEY_NONE,               HID_KEY_APPLICATION,                    KEYBOARD_MODIFIER_RIGHTCTRL,            HID_KEY_ARROW_LEFT,     HID_KEY_ARROW_DOWN,     HID_KEY_ARROW_RIGHT}
};

uint8_t current_keycodes[6][17];

void switch_keycodes(bool use_fn) {
    if (use_fn) {
        memcpy(current_keycodes, fn_keycodes, sizeof(fn_keycodes));
    } else {
        memcpy(current_keycodes, keycodes, sizeof(keycodes));
    }
}


bool is_modifier (uint8_t keycode, uint8_t output_index, uint8_t input_index) {
    bool normal_key_indexes = (
        (output_index == 0 && input_index == 8)     // HID_KEY_F7
        || (output_index == 1 && input_index == 3)  // HID_KEY_3
        || (output_index == 2 && input_index == 3)  // HID_KEY_E
        || (output_index == 3 && input_index == 1)  // HID_KEY_A
        || (output_index == 4 && input_index == 7)  // HID_KEY_M
    );

    for (
        hid_keyboard_modifier_bm_t modifier = KEYBOARD_MODIFIER_LEFTCTRL;
        modifier <= KEYBOARD_MODIFIER_RIGHTGUI;
        modifier <<=1
    ) {
        if (keycode == modifier && !normal_key_indexes) {
            return true;
        }
    }
    return false;
}


void change_mode(connection_mode_t mode) {
    save_mode(mode);
    current_mode = mode;
    esp_restart();
}


void change_mode_by_keycode(uint8_t keycode) {
    if (current_mode == MODE_USB) {
        if (keycode == HID_KEY_6) {
            change_mode(MODE_BLE);
        } else if (keycode == HID_KEY_7) {
            change_mode(MODE_WIRELESS);
        }
    }

    if (current_mode == MODE_BLE) {
        if (keycode == HID_KEY_5) {
            change_mode(MODE_USB);
        } else if (keycode == HID_KEY_7) {
            change_mode(MODE_WIRELESS);
        }
    }

    if (current_mode == MODE_WIRELESS) {
        if (keycode == HID_KEY_5) {
            change_mode(MODE_USB);
        } else if (keycode == HID_KEY_6) {
            change_mode(MODE_BLE);
        }
    }
}


void connect_new_ble_with_saving(uint8_t keycode) {
    if (keycode == HID_KEY_8) {
        current_ble_idx = 1;
    } else if (keycode == HID_KEY_9) {
        current_ble_idx = 2;
    } else if (keycode == HID_KEY_0) {
        current_ble_idx = 3;
    } else {
        return;
    }
    is_new_connection = true;
    disconnect_all_bonded_devices();
    show_bonded_devices();
    esp_ble_gap_start_advertising(&hidd_adv_params);
}


void handle_connected_ble_device(uint8_t keycode) {
    if (!use_fn) {
        return;
    }

    if (keycode == HID_KEY_GRAVE) {
        // Initialize the Bluetooth Connecton.
        delete_host_from_nvs(1);
        delete_host_from_nvs(2);
        delete_host_from_nvs(3);
        remove_all_bonded_devices();
        return;
    }

    if (keycode == HID_KEY_8) {
        current_ble_idx = 1;
    } else if (keycode == HID_KEY_9) {
        current_ble_idx = 2;
    } else if (keycode == HID_KEY_0) {
        current_ble_idx = 3;
    } else {
        return;
    }

    is_change_to_paired_device = true;
    load_host_from_nvs(current_ble_idx, &host_to_be_connected);
    if (memcmp(host_to_be_connected.bda, empty_host.bda, sizeof(esp_bd_addr_t)) == 0) {
        ESP_LOGI(__func__, "No device to connect");
        return;
    }

    disconnect_all_bonded_devices();
    esp_ble_gap_start_advertising(&hidd_adv_params);
}


void toggle_use_fn() {
    use_fn = !use_fn;
    switch_keycodes(use_fn);
}


void init_special_keys() {
    if (use_fn == true) {
        toggle_use_fn();
    }

    if (use_right_shift == true) {
        use_right_shift = false;
    }
}


void send_release_report() {
    uint8_t empty_key = 0;
    uint8_t empty_key_array[6] = {0};
    uint8_t espnow_release_key[8] = {0};

    if (current_mode == MODE_USB)
    {
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, empty_key_array);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        tud_hid_report(TUD_CONSUMER_CONTROL, &empty_key, 2);
    }
    else if (current_mode == MODE_BLE)
    {
        esp_hidd_send_keyboard_value(hid_conn_id, 0, &empty_key, 1);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        esp_hidd_send_consumer_value(hid_conn_id, empty_key, 1);
    }
    else if (current_mode == MODE_WIRELESS)
    {
        esp_now_send(peer_mac, espnow_release_key, 32);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        espnow_release_key[1] = 1;
        esp_now_send(peer_mac, espnow_release_key, 32);
    }
}


void get_espnow_send_data(uint8_t keycode, uint8_t modifier, uint8_t *espnow_send_data) {
    // Convert keycode to uint8_t array for esp_now_send
    char temp [6];
    uint8_t converted_data [6];

    sprintf(temp, "%d", keycode);
    memcpy(converted_data, temp, sizeof(temp));

    memset(espnow_send_data, 0, 8);

    espnow_send_data[0] = modifier;
    memcpy(&espnow_send_data[2], converted_data, sizeof(converted_data));
    if (use_fn) {
        espnow_send_data[1] = 1;
    }
}


void handle_pressed_key(keyboard_btn_report_t kbd_report, uint8_t *keycode, uint8_t *modifier) {
    // handle keycode, modifier, use_fn, use_right_shift by pressed_key
    for (int i = 0; i < kbd_report.key_pressed_num; i++) {
        uint32_t output_index = kbd_report.key_data[i].output_index;
        uint32_t input_index = kbd_report.key_data[i].input_index;
        *keycode = current_keycodes[output_index][input_index];
        
        // modifier handling
        if (is_modifier(*keycode, output_index, input_index)) {
            *modifier |= *keycode;
        }

        // use_fn handling
        if (output_index == 5 && input_index == 10) {
            if (use_fn == false) {
                toggle_use_fn();
            }
        }

        // use_right_shift handling
        if (*keycode == HID_KEY_SHIFT_RIGHT) {
            use_right_shift = true;
        }
    }

    // keycode handling
    uint32_t lpki = kbd_report.key_pressed_num - 1; // lpki stands for 'last pressed key index'
    uint32_t last_output_index = kbd_report.key_data[lpki].output_index;
    uint32_t last_input_index = kbd_report.key_data[lpki].input_index;
    *keycode = current_keycodes[last_output_index][last_input_index];
    if (is_modifier(*keycode, last_output_index, last_input_index)) {
        *keycode = 0;
    }
}


void keyboard_cb(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data)
{
    uint8_t keycode = 0;
    uint8_t modifier = 0;

    init_special_keys();

    if (kbd_report.key_change_num < 0) {
        send_release_report();
        return;
    }

    if (kbd_report.key_change_num > 0) {
        handle_pressed_key(kbd_report, &keycode, &modifier);

        if (use_fn) {
            change_mode_by_keycode(keycode);
        }

        if (current_mode == MODE_USB)
        {
            if (use_fn) {
                tud_hid_report(TUD_CONSUMER_CONTROL, &keycode, 2);
            } else {
                uint8_t key_array[6] = {keycode};
                tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, key_array);
            }
        }
        else if (current_mode == MODE_BLE)
        {
            ESP_LOGI(__func__, "use_fn, use_right_shift: %d, %d", use_fn, use_right_shift);
            if (use_fn && use_right_shift) {
                connect_new_ble_with_saving(keycode);
                return;
            }

            handle_connected_ble_device(keycode);
            
            if (use_fn) {
                esp_hidd_send_consumer_value(hid_conn_id, keycode, 1);
            } else {
                esp_hidd_send_keyboard_value(hid_conn_id, modifier, &keycode, 1);
            }
        }
        else if (current_mode == MODE_WIRELESS)
        {
            uint8_t espnow_send_data[8];
            get_espnow_send_data(keycode, modifier, espnow_send_data);
            esp_now_send(peer_mac, espnow_send_data, 8);
        }
    }
}


keyboard_btn_cb_config_t cb_cfg = {
    .event = KBD_EVENT_PRESSED,
    .callback = keyboard_cb,
};


void keyboard_task(void) {
    switch_keycodes(use_fn);
    keyboard_button_create(&cfg, &kbd_handle);
    keyboard_button_register_cb(kbd_handle, cb_cfg, NULL);
}