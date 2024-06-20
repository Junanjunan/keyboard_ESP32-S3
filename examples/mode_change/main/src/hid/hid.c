#include "esp_event.h"
#include "esp_log.h"
#include "hid_dev.h"
#include "keyboard_button.h"
#include "tinyusb.h"
#include "change_mode_interrupt.h"
#include "esp_now.h"
#include "esp_now_main.h"


static uint16_t hid_conn_id = 0;


keyboard_btn_config_t cfg = {
    .output_gpios = (int[])
    {
        37, 38, 39, 40, 41, 42
    },
    .output_gpio_num = 6,
    .input_gpios = (int[])
    {
        7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 47, 48, 45, 35
    },
    .input_gpio_num = 18,
    .active_level = 1,
    .debounce_ticks = 2,
    .ticks_interval = 500,      // us
    .enable_power_save = false, // enable power save
};


keyboard_btn_handle_t kbd_handle = NULL;
keyboard_btn_handle_t kbd_handle_combi_volume_decrement = NULL;
keyboard_btn_handle_t kbd_handle_combi_volume_increment = NULL;


// make function key at the bottom of F8 line (Current: HID_KEY_GUI_RIGHT)
uint8_t keycodes[6][18] = {
    {HID_KEY_ESCAPE,                HID_KEY_NONE,               HID_KEY_F1,                 HID_KEY_F2,     HID_KEY_F3,     HID_KEY_F4,      HID_KEY_NONE,  HID_KEY_F5,     HID_KEY_F6,     HID_KEY_F7,                 HID_KEY_F8,                 HID_KEY_F9,             HID_KEY_F10,            HID_KEY_F11,    HID_KEY_F12,                    HID_KEY_PRINT_SCREEN,   HID_KEY_SCROLL_LOCK,    HID_KEY_PAUSE},
    {HID_KEY_GRAVE,                 HID_KEY_1,                  HID_KEY_2,                  HID_KEY_3,      HID_KEY_4,      HID_KEY_5,       HID_KEY_6,     HID_KEY_7,      HID_KEY_8,      HID_KEY_9,                  HID_KEY_0,                  HID_KEY_MINUS,          HID_KEY_EQUAL,          HID_KEY_NONE,   HID_KEY_BACKSPACE,              HID_KEY_INSERT,         HID_KEY_HOME,           HID_KEY_PAGE_UP},
    {HID_KEY_TAB,                   HID_KEY_Q,                  HID_KEY_W,                  HID_KEY_E,      HID_KEY_R,      HID_KEY_T,       HID_KEY_Y,     HID_KEY_U,      HID_KEY_I,      HID_KEY_O,                  HID_KEY_P,                  HID_KEY_BRACKET_LEFT,   HID_KEY_BRACKET_RIGHT,  HID_KEY_NONE,   HID_KEY_BACKSLASH,              HID_KEY_DELETE,         HID_KEY_END,            HID_KEY_PAGE_DOWN},
    {HID_KEY_CAPS_LOCK,             HID_KEY_A,                  HID_KEY_S,                  HID_KEY_D,      HID_KEY_F,      HID_KEY_G,       HID_KEY_H,     HID_KEY_J,      HID_KEY_K,      HID_KEY_L,                  HID_KEY_SEMICOLON,          HID_KEY_APOSTROPHE,     HID_KEY_NONE,           HID_KEY_NONE,   HID_KEY_ENTER,                  HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTSHIFT,   HID_KEY_Z,                  HID_KEY_X,                  HID_KEY_C,      HID_KEY_V,      HID_KEY_B,       HID_KEY_N,     HID_KEY_M,      HID_KEY_COMMA,  HID_KEY_PERIOD,             HID_KEY_SLASH,              HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE,   HID_KEY_SHIFT_RIGHT,            HID_KEY_NONE,           HID_KEY_ARROW_UP,       HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTCTRL,    KEYBOARD_MODIFIER_LEFTGUI,  KEYBOARD_MODIFIER_LEFTALT,  HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_SPACE,   HID_KEY_NONE,  HID_KEY_NONE,   HID_KEY_NONE,   KEYBOARD_MODIFIER_RIGHTALT, HID_KEY_NONE,               HID_KEY_APPLICATION,    HID_KEY_NONE,           HID_KEY_NONE,   KEYBOARD_MODIFIER_RIGHTCTRL,    HID_KEY_ARROW_LEFT,     HID_KEY_ARROW_DOWN,     HID_KEY_ARROW_RIGHT}
};


bool is_modifier (uint8_t keycode, uint8_t output_index, uint8_t input_index) {
    bool normal_key_indexes = (
        (output_index == 0 && input_index == 9)     // HID_KEY_F7
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


void keyboard_cb(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data)
{
    uint8_t keycode = 0;
    uint8_t key[6] = {keycode};
    uint8_t espnow_release_key [] = "0";
    uint8_t modifier = 0;
    if (kbd_report.key_change_num < 0) {
        if (current_mode == MODE_USB)
        {
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, key);
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

    if (kbd_report.key_change_num > 0) {
        uint32_t lpki = kbd_report.key_pressed_num - 1; // lpki stands for 'last pressed key index'
        uint32_t last_output_index = kbd_report.key_data[lpki].output_index;
        uint32_t last_input_index = kbd_report.key_data[lpki].input_index;
        keycode = keycodes[last_output_index][last_input_index];
        ESP_LOGI(__func__, "pressed_keycode: %x", keycode);
        if (is_modifier(keycode, last_output_index, last_input_index)) {
            modifier |= keycode;
            keycode = 0;
        }
        uint8_t key[6] = {keycode};

        // Convert keycode to uint8_t array for esp_now_send
        char temp [6];
        uint8_t converted_data [6];
        sprintf(temp, "%d", keycode);
        memcpy(converted_data, temp, sizeof(temp));

        if (current_mode == MODE_USB)
        {
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, key);
        }
        else if (current_mode == MODE_BLE)
        {
            esp_hidd_send_keyboard_value(hid_conn_id, modifier, &keycode, 1);
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


static void combi_volume_decrement(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data)
{
    uint16_t keycode = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
    esp_hidd_send_consumer_value(hid_conn_id, keycode, 1);
}


keyboard_btn_cb_config_t cb_cfg_volume_decrement = {
    .event = KBD_EVENT_COMBINATION,
    .callback = combi_volume_decrement,
    .event_data.combination.key_num = 2,
    .event_data.combination.key_data = (keyboard_btn_data_t[]) {
        {5, 10},    // Fn
        {0, 13},    // F11
    },
};


static void combi_volume_increment(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data)
{
    uint16_t keycode = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
    esp_hidd_send_consumer_value(hid_conn_id, keycode, 1);
}


keyboard_btn_cb_config_t cb_cfg_volume_increment = {
    .event = KBD_EVENT_COMBINATION,
    .callback = combi_volume_increment,
    .event_data.combination.key_num = 2,
    .event_data.combination.key_data = (keyboard_btn_data_t[]) {
        {5, 10},    // Fn
        {0, 14},    // F12
    },
};


void keyboard_task(void) {
    keyboard_button_create(&cfg, &kbd_handle);
    keyboard_button_create(&cfg, &kbd_handle_combi_volume_decrement);
    keyboard_button_create(&cfg, &kbd_handle_combi_volume_increment);

    keyboard_button_register_cb(kbd_handle, cb_cfg, NULL);
    keyboard_button_register_cb(kbd_handle_combi_volume_decrement, cb_cfg_volume_decrement, NULL);
    keyboard_button_register_cb(kbd_handle_combi_volume_increment, cb_cfg_volume_increment, NULL);    
}