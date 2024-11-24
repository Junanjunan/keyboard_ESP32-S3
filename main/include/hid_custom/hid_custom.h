#include "esp_gap_ble_api.h"
#include "keyboard_button.h"


extern esp_ble_bond_dev_t * dev_list_before_new_connection;

extern int dev_num_before_new_connection;

void keyboard_cb(keyboard_btn_handle_t kbd_handle, keyboard_btn_report_t kbd_report, void *user_data);

void keyboard_task(void);