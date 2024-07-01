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

char *bda_to_string(esp_bd_addr_t bda, char *str, size_t size);

esp_err_t save_host_to_nvs(int index, bt_host_info_t *host);

esp_err_t load_host_from_nvs(int index, bt_host_info_t *host);

void ble_main(void);