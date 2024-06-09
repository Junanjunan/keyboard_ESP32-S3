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
keyboard_btn_handle_t kbd_handle_combi = NULL;


// uint8_t keycodes[2][2] = {
//     {HID_KEY_A, HID_KEY_B},  // HID keycodes for 'a', 'b'
//     {HID_KEY_C, HID_KEY_D}   // HID keycodes for 'c', 'd'
// };

// keycodes for the keyboard 6*18
// uint8_t keycodes[6][18] = {
//     {HID_KEY_A,         HID_KEY_B,          HID_KEY_C,              HID_KEY_D,              HID_KEY_E,              HID_KEY_F,                  HID_KEY_G,          HID_KEY_H,              HID_KEY_I,                  HID_KEY_J,                  HID_KEY_K,                  HID_KEY_L,              HID_KEY_M,              HID_KEY_N,              HID_KEY_O,          HID_KEY_P,          HID_KEY_Q,          HID_KEY_R},
//     {HID_KEY_S,         HID_KEY_T,          HID_KEY_U,              HID_KEY_V,              HID_KEY_W,              HID_KEY_X,                  HID_KEY_Y,          HID_KEY_Z,              HID_KEY_1,                  HID_KEY_2,                  HID_KEY_3,                  HID_KEY_4,              HID_KEY_5,              HID_KEY_6,              HID_KEY_7,          HID_KEY_8,          HID_KEY_9,          HID_KEY_0},
//     {HID_KEY_ENTER,     HID_KEY_ESCAPE,     HID_KEY_BACKSPACE,      HID_KEY_TAB,            HID_KEY_SPACE,          HID_KEY_MINUS,              HID_KEY_EQUAL,      HID_KEY_BRACKET_LEFT,   HID_KEY_BRACKET_RIGHT,      HID_KEY_BACKSLASH,          HID_KEY_EUROPE_1,           HID_KEY_SEMICOLON,      HID_KEY_APOSTROPHE,     HID_KEY_GRAVE,          HID_KEY_COMMA,      HID_KEY_PERIOD,     HID_KEY_SLASH,      HID_KEY_CAPS_LOCK},
//     {HID_KEY_F1,        HID_KEY_F2,         HID_KEY_F3,             HID_KEY_F4,             HID_KEY_F5,             HID_KEY_F6,                 HID_KEY_F7,         HID_KEY_F8,             HID_KEY_F9,                 HID_KEY_F10,                HID_KEY_F11,                HID_KEY_F12,            HID_KEY_PRINT_SCREEN,   HID_KEY_SCROLL_LOCK,    HID_KEY_PAUSE,      HID_KEY_INSERT,     HID_KEY_HOME,       HID_KEY_PAGE_UP,},
//     {HID_KEY_DELETE,    HID_KEY_END,        HID_KEY_PAGE_DOWN,      HID_KEY_ARROW_RIGHT,    HID_KEY_ARROW_LEFT,     HID_KEY_ARROW_DOWN,         HID_KEY_ARROW_UP,   HID_KEY_NUM_LOCK,       HID_KEY_KEYPAD_DIVIDE,      HID_KEY_KEYPAD_MULTIPLY,    HID_KEY_KEYPAD_SUBTRACT,    HID_KEY_KEYPAD_ADD,     HID_KEY_KEYPAD_ENTER,   HID_KEY_KEYPAD_1,       HID_KEY_KEYPAD_2,   HID_KEY_KEYPAD_3,   HID_KEY_KEYPAD_4,   HID_KEY_KEYPAD_5,},
//     {HID_KEY_KEYPAD_6,  HID_KEY_KEYPAD_7,   HID_KEY_KEYPAD_8,       HID_KEY_KEYPAD_9,       HID_KEY_KEYPAD_0,       HID_KEY_KEYPAD_DECIMAL,     HID_KEY_EUROPE_2,   HID_KEY_APPLICATION,    HID_KEY_POWER,              HID_KEY_KEYPAD_EQUAL,       HID_KEY_F13,                HID_KEY_F14,            HID_KEY_F15,            HID_KEY_F16,            HID_KEY_F17,        HID_KEY_F18,        HID_KEY_F19,        HID_KEY_F20,}
// };

// uint8_t keycodes[6][22] = {
//     {HID_KEY_ESCAPE,        HID_KEY_NONE,       HID_KEY_F1,        HID_KEY_F2,         HID_KEY_F3,             HID_KEY_F4,      HID_KEY_NONE,        HID_KEY_F5,        HID_KEY_F6,     HID_KEY_F7,         HID_KEY_F8,             HID_KEY_F9,             HID_KEY_F10,                HID_KEY_F11,        HID_KEY_F12,            HID_KEY_PRINT_SCREEN,   HID_KEY_SCROLL_LOCK,    HID_KEY_PAUSE,          HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE,               HID_KEY_NONE},
//     {HID_KEY_GRAVE,         HID_KEY_1,          HID_KEY_2,         HID_KEY_3,          HID_KEY_4,              HID_KEY_5,       HID_KEY_6,           HID_KEY_7,         HID_KEY_8,      HID_KEY_9,          HID_KEY_0,              HID_KEY_MINUS,          HID_KEY_EQUAL,              HID_KEY_NONE,       HID_KEY_BACKSPACE,      HID_KEY_INSERT,         HID_KEY_HOME,           HID_KEY_PAGE_UP,        HID_KEY_NUM_LOCK,       HID_KEY_KEYPAD_DIVIDE,  HID_KEY_KEYPAD_MULTIPLY,    HID_KEY_KEYPAD_SUBTRACT},
//     {HID_KEY_TAB,           HID_KEY_Q,          HID_KEY_W,         HID_KEY_E,          HID_KEY_R,              HID_KEY_T,       HID_KEY_Y,           HID_KEY_U,         HID_KEY_I,      HID_KEY_O,          HID_KEY_P,              HID_KEY_BRACKET_LEFT,   HID_KEY_BRACKET_RIGHT,      HID_KEY_NONE,       HID_KEY_BACKSLASH,      HID_KEY_DELETE,         HID_KEY_END,            HID_KEY_PAGE_DOWN,      HID_KEY_KEYPAD_7,       HID_KEY_KEYPAD_8,       HID_KEY_KEYPAD_9,           HID_KEY_KEYPAD_ADD},
//     {HID_KEY_CAPS_LOCK,     HID_KEY_A,          HID_KEY_S,         HID_KEY_D,          HID_KEY_F,              HID_KEY_G,       HID_KEY_H,           HID_KEY_J,         HID_KEY_K,      HID_KEY_L,          HID_KEY_SEMICOLON,      HID_KEY_APOSTROPHE,     HID_KEY_NONE,               HID_KEY_NONE,       HID_KEY_ENTER,          HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_KEYPAD_4,       HID_KEY_KEYPAD_5,       HID_KEY_KEYPAD_6,           HID_KEY_NONE},
//     {HID_KEY_SHIFT_LEFT,    HID_KEY_Z,          HID_KEY_X,         HID_KEY_C,          HID_KEY_V,              HID_KEY_B,       HID_KEY_N,           HID_KEY_M,         HID_KEY_COMMA,  HID_KEY_PERIOD,     HID_KEY_SLASH,          HID_KEY_NONE,           HID_KEY_NONE,               HID_KEY_NONE,       HID_KEY_SHIFT_RIGHT,    HID_KEY_NONE,           HID_KEY_ARROW_UP,       HID_KEY_NONE,           HID_KEY_KEYPAD_1,       HID_KEY_KEYPAD_2,       HID_KEY_KEYPAD_3,           HID_KEY_KEYPAD_ENTER},
//     {HID_KEY_CONTROL_LEFT,  HID_KEY_GUI_LEFT,   HID_KEY_ALT_LEFT,  HID_KEY_NONE,       HID_KEY_NONE,           HID_KEY_SPACE,   HID_KEY_NONE,        HID_KEY_NONE,      HID_KEY_NONE,   HID_KEY_ALT_RIGHT,  펑션,                   HID_KEY_APPLICATION,    HID_KEY_NONE,               HID_KEY_NONE,       HID_KEY_CONTROL_RIGHT,  HID_KEY_ARROW_LEFT,     HID_KEY_ARROW_DOWN,     HID_KEY_ARROW_RIGHT,    HID_KEY_KEYPAD_0,       HID_KEY_NONE,           HID_KEY_KEYPAD_DECIMAL,     HID_KEY_NONE}
// };

// make function key at the bottom of F8 line (Current: HID_KEY_GUI_RIGHT)
uint8_t keycodes[6][18] = {
    {HID_KEY_ESCAPE,                HID_KEY_NONE,               HID_KEY_F1,                 HID_KEY_F2,     HID_KEY_F3,     HID_KEY_F4,      HID_KEY_NONE,  HID_KEY_F5,     HID_KEY_F6,     HID_KEY_F7,                 HID_KEY_F8,                 HID_KEY_F9,             HID_KEY_F10,            HID_KEY_F11,    HID_KEY_F12,                    HID_KEY_PRINT_SCREEN,   HID_KEY_SCROLL_LOCK,    HID_KEY_PAUSE},
    {HID_KEY_GRAVE,                 HID_KEY_1,                  HID_KEY_2,                  HID_KEY_3,      HID_KEY_4,      HID_KEY_5,       HID_KEY_6,     HID_KEY_7,      HID_KEY_8,      HID_KEY_9,                  HID_KEY_0,                  HID_KEY_MINUS,          HID_KEY_EQUAL,          HID_KEY_NONE,   HID_KEY_BACKSPACE,              HID_KEY_INSERT,         HID_KEY_HOME,           HID_KEY_PAGE_UP},
    {HID_KEY_TAB,                   HID_KEY_Q,                  HID_KEY_W,                  HID_KEY_E,      HID_KEY_R,      HID_KEY_T,       HID_KEY_Y,     HID_KEY_U,      HID_KEY_I,      HID_KEY_O,                  HID_KEY_P,                  HID_KEY_BRACKET_LEFT,   HID_KEY_BRACKET_RIGHT,  HID_KEY_NONE,   HID_KEY_BACKSLASH,              HID_KEY_DELETE,         HID_KEY_END,            HID_KEY_PAGE_DOWN},
    {HID_KEY_CAPS_LOCK,             HID_KEY_A,                  HID_KEY_S,                  HID_KEY_D,      HID_KEY_F,      HID_KEY_G,       HID_KEY_H,     HID_KEY_J,      HID_KEY_K,      HID_KEY_L,                  HID_KEY_SEMICOLON,          HID_KEY_APOSTROPHE,     HID_KEY_NONE,           HID_KEY_NONE,   HID_KEY_ENTER,                  HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTSHIFT,   HID_KEY_Z,                  HID_KEY_X,                  HID_KEY_C,      HID_KEY_V,      HID_KEY_B,       HID_KEY_N,     HID_KEY_M,      HID_KEY_COMMA,  HID_KEY_PERIOD,             HID_KEY_SLASH,              HID_KEY_NONE,           HID_KEY_NONE,           HID_KEY_NONE,   HID_KEY_SHIFT_RIGHT,            HID_KEY_NONE,           HID_KEY_ARROW_UP,       HID_KEY_NONE},
    {KEYBOARD_MODIFIER_LEFTCTRL,    KEYBOARD_MODIFIER_LEFTGUI,  KEYBOARD_MODIFIER_LEFTALT,  HID_KEY_NONE,   HID_KEY_NONE,   HID_KEY_SPACE,   HID_KEY_NONE,  HID_KEY_NONE,   HID_KEY_NONE,   KEYBOARD_MODIFIER_RIGHTALT, KEYBOARD_MODIFIER_RIGHTGUI, HID_KEY_APPLICATION,    HID_KEY_NONE,           HID_KEY_NONE,   KEYBOARD_MODIFIER_RIGHTCTRL,    HID_KEY_ARROW_LEFT,     HID_KEY_ARROW_DOWN,     HID_KEY_ARROW_RIGHT}
};


void decimalToHexadecimal(int num, char *hexStr) {
    int i = 0;
    int temp;
    // Process the number until it becomes 0
    while (num != 0) {
        temp = num % 16;  // Get the remainder
        
        // Convert numerical value to hexadecimal value
        if (temp < 10) {
            hexStr[i++] = temp + '0';
        } else {
            hexStr[i++] = temp - 10 + 'A';
        }

        num = num / 16;  // Reduce the number
    }

    hexStr[i] = '\0';  // Null terminate the string

    // Reverse the string since the digits are in reverse order
    int start = 0;
    int end = i - 1;
    char tmp;
    while (start < end) {
        tmp = hexStr[start];
        hexStr[start] = hexStr[end];
        hexStr[end] = tmp;
        start++;
        end--;
    }
}


bool is_modifier (uint8_t keycode) {
    for (hid_keyboard_modifier_bm_t modifier = KEYBOARD_MODIFIER_LEFTCTRL; modifier < 8; modifier ++) {
        if (keycode == modifier) {
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
    uint8_t special_keys = KEYBOARD_MODIFIER_LEFTSHIFT;
    uint8_t modifier = 0;
    if (kbd_report.key_pressed_num == 0) {
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

    for (int i = 0; i < kbd_report.key_pressed_num; i++) {
        keycode = keycodes[kbd_report.key_data[i].output_index][kbd_report.key_data[i].input_index];
        if (is_modifier(keycode)) {
            modifier |= keycode;
            keycode = 0;
        }
        uint8_t key[6] = {keycode};

        char hexStr[20];  // Allocate enough space for the largest possible hex number
        decimalToHexadecimal(keycode, hexStr);
        printf("Decimal: %d is Hexadecimal: %s\n", keycode, hexStr);

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


static void keyboard_combination_cb1(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data)
{
    printf("combination 1: ");
    for (int i = 0; i < kbd_report.key_pressed_num; i++) {
        printf("(%d,%d) ", kbd_report.key_data[i].output_index, kbd_report.key_data[i].input_index);
    }
    printf("\n\n");
}

keyboard_btn_cb_config_t cb_cfg_combi = {
    .event = KBD_EVENT_COMBINATION,
    .callback = keyboard_combination_cb1,
    .event_data.combination.key_num = 2,
    .event_data.combination.key_data = (keyboard_btn_data_t[]) {
        {4, 1},
        {4, 2},
    },
};


void keyboard_task(void) {
    keyboard_button_create(&cfg, &kbd_handle);
    keyboard_button_create(&cfg, &kbd_handle_combi);
    keyboard_button_register_cb(kbd_handle, cb_cfg, NULL);
    keyboard_button_register_cb(kbd_handle_combi, cb_cfg_combi, NULL);
}