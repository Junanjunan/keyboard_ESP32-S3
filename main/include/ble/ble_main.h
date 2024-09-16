#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"

#define MAX_BT_DEVICENAME_LENGTH 20
#define NVS_KEY_BASE "bt_host_"


// Structure to hold BT device info
typedef struct {
    esp_bd_addr_t bda;
    char name[MAX_BT_DEVICENAME_LENGTH + 1];
} bt_host_info_t;

extern esp_ble_adv_params_t hidd_adv_params;

extern int32_t current_ble_idx;

extern bool is_new_connection;

extern bool is_disconnect_by_keyboard;

extern bool is_change_to_paired_device;

extern bt_host_info_t host_to_be_connected;

extern bt_host_info_t empty_host;

void show_bonded_devices(void);

char *bda_to_string(esp_bd_addr_t bda, char *str, size_t size);

int32_t get_saved_ble_idx(void);

void save_ble_idx(int32_t ble_idx);

void disconnect_all_bonded_devices(void);

void remove_all_bonded_devices(void);

void remove_unpaired_devices(void);

void connect_allowed_device(esp_bd_addr_t allowed_bda);

void remove_unsaved_pairing_device(void);

esp_err_t save_host_to_nvs(int index, bt_host_info_t *host);

esp_err_t load_host_from_nvs(int index, bt_host_info_t *host);

esp_err_t delete_host_from_nvs(int index);

void ble_main(void);