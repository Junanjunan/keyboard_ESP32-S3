/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "driver/gpio.h"
#include "hid_dev.h"
#include "hid_custom.h"
#include "ble_main.h"
#include "esp_mac.h"


/**
 * Brief:
 * This example Implemented BLE HID device profile related functions, in which the HID device
 * has 4 Reports (1 is mouse, 2 is keyboard and LED, 3 is Consumer Devices, 4 is Vendor devices).
 * Users can choose different reports according to their own application scenarios.
 * BLE HID profile inheritance and USB HID class.
 */

/**
 * Note:
 * 1. Win10 does not support vendor report , So SUPPORT_REPORT_VENDOR is always set to FALSE, it defines in hidd_le_prf_int.h
 * 2. Update connection parameters are not allowed during iPhone HID encryption, slave turns
 * off the ability to automatically update connection parameters during encryption.
 * 3. After our HID device is connected, the iPhones write 1 to the Report Characteristic Configuration Descriptor,
 * even if the HID encryption is not completed. This should actually be written 1 after the HID encryption is completed.
 * we modify the permissions of the Report Characteristic Configuration Descriptor to `ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED`.
 * if you got `GATT_INSUF_ENCRYPTION` error, please ignore.
 */

#define HID_DEMO_TAG "HID_DEMO"


static uint16_t hid_conn_id = 0;
static bool sec_conn = false;
#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);

#define HIDD_DEVICE_NAME            "ESP32 BLE Keyboard"
#define HID_APPEARANCE_KEYBOARD     0x03C1
static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

esp_bd_addr_t current_bda;

int32_t current_ble_idx = 0;

bool is_new_connection = false;

bool is_disconnect_by_keyboard = false;

bool is_bonded_addr_removed = false;

bool is_change_to_paired_device = false;

bt_host_info_t host_to_be_connected;


/*
 * Supplement to the Bluetooth Core Specification
 * Source: https://www.bluetooth.com/specifications/specs/core-specification-supplement-9/
    - Reference: AD in 1 DATA TYPES DEFINITIONS AND FORMATS Table 1.1.
    - Downloaded pdf: docs/advertising_data/CSS_v9.pdf (page 9)
*/
static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = HID_APPEARANCE_KEYBOARD,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};


esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x30,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};


int32_t get_saved_ble_idx() {
    int32_t ble_idx = 0;

    nvs_flash_init();
    nvs_handle_t nvs_ble_idx_handle;
    nvs_open("storage", NVS_READONLY, &nvs_ble_idx_handle);
    nvs_get_i32(nvs_ble_idx_handle, "ble_idx", &ble_idx);
    nvs_close(nvs_ble_idx_handle);
    ESP_LOGI(__func__, "Loaded ble_idx is %ld", ble_idx);
    return ble_idx;
}


void save_ble_idx(int32_t ble_idx) {
    nvs_flash_init();
    nvs_handle_t nvs_ble_idx_handle;
    nvs_open("storage", NVS_READWRITE, &nvs_ble_idx_handle);
    nvs_set_i32(nvs_ble_idx_handle, "ble_idx", ble_idx);
    nvs_commit(nvs_ble_idx_handle);
    nvs_close(nvs_ble_idx_handle);
    ESP_LOGI(__func__, "Saved ble_idx is %ld", ble_idx);
}

void show_bonded_devices(void)
{
    ESP_LOGI(HID_DEMO_TAG, "current ble_idx: %ld", current_ble_idx);

    bt_host_info_t host;
    load_host_from_nvs(1, &host);
    ESP_LOGI(HID_DEMO_TAG, "index 1 - Saved addr!!!: %02x:%02x:%02x:%02x:%02x:%02x",
                     host.bda[0], host.bda[1], host.bda[2], host.bda[3], host.bda[4], host.bda[5]);
    load_host_from_nvs(2, &host);
    ESP_LOGI(HID_DEMO_TAG, "index 2 - Saved addr!!!: %02x:%02x:%02x:%02x:%02x:%02x",
                     host.bda[0], host.bda[1], host.bda[2], host.bda[3], host.bda[4], host.bda[5]);
    load_host_from_nvs(3, &host);
    ESP_LOGI(HID_DEMO_TAG, "index 3 - Saved addr!!!: %02x:%02x:%02x:%02x:%02x:%02x",
                     host.bda[0], host.bda[1], host.bda[2], host.bda[3], host.bda[4], host.bda[5]);

    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num == 0) {
        ESP_LOGI(__func__, "Bonded devices number zero\n");
        return;
    }

    esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    if (!dev_list) {
        ESP_LOGE(__func__, "malloc failed, return\n");
        return;
    }

    esp_ble_get_bond_device_list(&dev_num, dev_list);
    ESP_LOGI(__func__, "Bonded devices list : %d", dev_num);
    for (int i = 0; i < dev_num; i++) {
        esp_log_buffer_hex(__func__, (void *)dev_list[i].bd_addr, sizeof(esp_bd_addr_t));
    }

    free(dev_list);
}


void disconnect_all_bonded_devices(void) {
    is_new_connection = true;
    is_disconnect_by_keyboard = true;

    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num == 0) {
        ESP_LOGI(__func__, "Bonded devices number zero\n");
        return;
    }

    esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    if (!dev_list) {
        ESP_LOGE(__func__, "malloc failed, return\n");
        return;
    }

    esp_ble_get_bond_device_list(&dev_num, dev_list);
    for (int i = 0; i < dev_num; i++) {
        esp_ble_gap_disconnect(dev_list[i].bd_addr);
    }
    free(dev_list);
}


void modify_removed_status_task (void) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    is_bonded_addr_removed = false;
    vTaskDelete(NULL);
}


static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_REG_FINISH");
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);
                esp_ble_gap_config_adv_data(&hidd_adv_data);

            }
            break;
        }
        case ESP_BAT_EVENT_REG: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_BAT_EVENT_REG");
            break;
        }
        case ESP_HIDD_EVENT_DEINIT_FINISH:
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_DEINIT_FINISH");
	        break;
		case ESP_HIDD_EVENT_BLE_CONNECT: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_CONNECT");
            hid_conn_id = param->connect.conn_id;
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_CONNECT, remote_bda %02x:%02x:%02x:%02x:%02x:%02x",
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
            sec_conn = false;
            char bda_str[18];
            memset(hidd_adv_params.peer_addr, 0, sizeof(hidd_adv_params.peer_addr));
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;
        }
        case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
            ESP_LOGI(HID_DEMO_TAG, "%s, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
            ESP_LOG_BUFFER_HEX(HID_DEMO_TAG, param->vendor_write.data, param->vendor_write.length);
            break;
        }
        case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT");
            ESP_LOG_BUFFER_HEX(HID_DEMO_TAG, param->led_write.data, param->led_write.length);
            break;
        }
        default:
            break;
    }
    return;
}


// Function to save host address to NVS
esp_err_t save_host_to_nvs(int index, bt_host_info_t *host) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    char key[15];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_BASE, index);
    
    err = nvs_set_blob(nvs_handle, key, host, sizeof(bt_host_info_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    ESP_LOGI(__func__, "Saved host bda %02x:%02x:%02x:%02x:%02x:%02x",
             host->bda[0], host->bda[1], host->bda[2], host->bda[3], host->bda[4], host->bda[5]);

    nvs_close(nvs_handle);
    return err;
}


// Function to load host address from NVS
esp_err_t load_host_from_nvs(int index, bt_host_info_t *host) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    char key[15];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_BASE, index);
    
    size_t required_size = sizeof(bt_host_info_t);
    err = nvs_get_blob(nvs_handle, key, host, &required_size);

    nvs_close(nvs_handle);
    return err;
}


// Function to convert BDA to string
char *bda_to_string(esp_bd_addr_t bda, char *str, size_t size) {
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}


static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    bt_host_info_t loaded_host;
    esp_bd_addr_t bd_addr;
    char* bd_name = "Host_2\0";
    size_t name_len = strlen(bd_name);
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT~!");
            load_host_from_nvs(0, &loaded_host);
            char bda_str[18];
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            for(int i = 0; i < ESP_BD_ADDR_LEN; i++) {
                ESP_LOGD(HID_DEMO_TAG, "%x:",param->ble_security.ble_req.bd_addr[i]);
            }
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            sec_conn = true;
            ESP_LOGI(
                "ESP_GAP_BLE_AUTH_CMPL_EVT", "peer_addr: %s",
                bda_to_string(param->ble_security.auth_cmpl.bd_addr, bda_str, sizeof(bda_str))
            );
            ESP_LOGI(HID_DEMO_TAG, "key_present = %d",param->ble_security.auth_cmpl.key_present);
            ESP_LOGI(HID_DEMO_TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
            ESP_LOGI(HID_DEMO_TAG, "pair status = %s",param->ble_security.auth_cmpl.success ? "success" : "fail");
            ESP_LOGI(HID_DEMO_TAG, "key_type = %d",param->ble_security.auth_cmpl.key_type);
            ESP_LOGI(HID_DEMO_TAG, "dev_type = %d",param->ble_security.auth_cmpl.dev_type);
            ESP_LOGI(HID_DEMO_TAG, "auth_mode = %d",param->ble_security.auth_cmpl.auth_mode);
            if(!param->ble_security.auth_cmpl.success) {
                ESP_LOGE(HID_DEMO_TAG, "fail reason = 0x%x",param->ble_security.auth_cmpl.fail_reason);
            } else {
                ESP_LOGI(HID_DEMO_TAG, "success~!~!~!");
                char host_name[20];
                snprintf(host_name, sizeof(host_name), "Host_%ld", current_ble_idx);
                bt_host_info_t connected_host;
                memcpy(connected_host.bda, param->ble_security.auth_cmpl.bd_addr, sizeof(connected_host.bda));
                strncpy(connected_host.name, host_name, MAX_BT_DEVICENAME_LENGTH);
                connected_host.name[MAX_BT_DEVICENAME_LENGTH] = '\0';

                save_ble_idx(current_ble_idx);
                save_host_to_nvs(current_ble_idx, &connected_host);

                esp_ble_gap_stop_advertising();

                is_new_connection = false;
                is_disconnect_by_keyboard = false;
                is_change_to_paired_device = false;
            }
            memcpy(current_bda, param->ble_security.auth_cmpl.bd_addr, sizeof(current_bda));
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_RESULT_EVT");
            break;
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT");
            if (is_bonded_addr_removed) {
                xTaskCreate(modify_removed_status_task, "modify_removed_status_task", 2048, NULL, 5, NULL);
            }
            break;
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_START_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_KEY_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_KEY_EVT");
            break;
        case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PASSKEY_NOTIF_EVT");
            break;
        case ESP_GAP_BLE_PASSKEY_REQ_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PASSKEY_REQ_EVT");
            break;
        case ESP_GAP_BLE_OOB_REQ_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_OOB_REQ_EVT");
            break;
        case ESP_GAP_BLE_LOCAL_IR_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_LOCAL_IR_EVT");
            break;
        case ESP_GAP_BLE_LOCAL_ER_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_LOCAL_ER_EVT");
            break;
        case ESP_GAP_BLE_NC_REQ_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_NC_REQ_EVT");
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT");
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT");
            break;
        case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_CLEAR_BOND_DEV_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_CLEAR_BOND_DEV_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_GET_BOND_DEV_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_GET_BOND_DEV_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_UPDATE_WHITELIST_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_UPDATE_DUPLICATE_EXCEPTIONAL_LIST_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_UPDATE_DUPLICATE_EXCEPTIONAL_LIST_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_CHANNELS_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_CHANNELS_EVT");
            break;
        case ESP_GAP_BLE_READ_PHY_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_READ_PHY_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_PREFERRED_DEFAULT_PHY_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_PREFERRED_DEFAULT_PHY_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_PREFERRED_PHY_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_PREFERRED_PHY_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_SET_CLEAR_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_SET_CLEAR_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_CREATE_SYNC_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_CREATE_SYNC_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_CANCEL_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SYNC_CANCEL_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_TERMINATE_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SYNC_TERMINATE_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_ADD_DEV_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_ADD_DEV_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_REMOVE_DEV_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_REMOVE_DEV_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_CLEAR_DEV_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_CLEAR_DEV_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PREFER_EXT_CONN_PARAMS_SET_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PREFER_EXT_CONN_PARAMS_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EXT_ADV_REPORT_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EXT_ADV_REPORT_EVT");
            break;
        case ESP_GAP_BLE_SCAN_TIMEOUT_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_TIMEOUT_EVT");
            break;
        case ESP_GAP_BLE_ADV_TERMINATED_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_ADV_TERMINATED_EVT");
            break;
        case ESP_GAP_BLE_SCAN_REQ_RECEIVED_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SCAN_REQ_RECEIVED_EVT");
            break;
        case ESP_GAP_BLE_CHANNEL_SELECT_ALGORITHM_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_CHANNEL_SELECT_ALGORITHM_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_REPORT_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_REPORT_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_LOST_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SYNC_LOST_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_ESTAB_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SYNC_ESTAB_EVT");
            break;
        case ESP_GAP_BLE_SC_OOB_REQ_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SC_OOB_REQ_EVT");
            break;
        case ESP_GAP_BLE_SC_CR_LOC_OOB_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SC_CR_LOC_OOB_EVT");
            break;
        case ESP_GAP_BLE_GET_DEV_NAME_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_GET_DEV_NAME_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_RECV_ENABLE_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_RECV_ENABLE_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_TRANS_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SYNC_TRANS_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SET_INFO_TRANS_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SET_INFO_TRANS_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SET_PAST_PARAMS_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_SET_PAST_PARAMS_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_PERIODIC_ADV_SYNC_TRANS_RECV_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_PERIODIC_ADV_SYNC_TRANS_RECV_EVT");
            break;
        case ESP_GAP_BLE_DTM_TEST_UPDATE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_DTM_TEST_UPDATE_EVT");
            break;
        case ESP_GAP_BLE_ADV_CLEAR_COMPLETE_EVT:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_ADV_CLEAR_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_EVT_MAX:
            ESP_LOGI(HID_DEMO_TAG, "ESP_GAP_BLE_EVT_MAX");
            break;
        default:
            break;
    }
}


void ble_main(void)
{
    esp_err_t ret;

    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    bt_host_info_t loaded_host;
    char bda_str[18];
    if (load_host_from_nvs(0, &loaded_host) == ESP_OK) {
        ESP_LOGI(
            __func__, "Host - 0: %s, Address: %s", 
            loaded_host.name, 
            bda_to_string(loaded_host.bda, bda_str, sizeof(bda_str))
        );
    }

    if (load_host_from_nvs(1, &loaded_host) == ESP_OK) {
        ESP_LOGI(
            __func__, "Host - 1: %s, Address: %s", 
            loaded_host.name, 
            bda_to_string(loaded_host.bda, bda_str, sizeof(bda_str))
        );
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s initialize controller failed", __func__);
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s enable controller failed", __func__);
        return;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed", __func__);
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed", __func__);
        return;
    }

    if((ret = esp_hidd_profile_init()) != ESP_OK) {
        ESP_LOGE(HID_DEMO_TAG, "%s init bluedroid failed", __func__);
    }

    ///register the callback function to the gap module
    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;           //set the IO capability to No output No input
    uint8_t key_size = 16;      //the key size should be 7~16 bytes
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribute to you,
    and the response key means which key you can distribute to the Master;
    If your BLE device act as a master, the response key means you hope which types of key of the slave should distribute to you,
    and the init key means which key you can distribute to the slave. */
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

}
