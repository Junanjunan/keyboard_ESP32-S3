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
#include "hid.h"


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

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x30,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};


static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_REG_FINISH");
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                //esp_bd_addr_t rand_addr = {0x04,0x11,0x11,0x11,0x11,0x05};
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
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {
            ESP_LOGI(HID_DEMO_TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
            sec_conn = false;
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

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
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
            esp_bd_addr_t bd_addr;
            memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
            ESP_LOGI(HID_DEMO_TAG, "remote BD_ADDR: %08x%04x",\
                    (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) + bd_addr[3],
                    (bd_addr[4] << 8) + bd_addr[5]);
            ESP_LOGI(HID_DEMO_TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
            ESP_LOGI(HID_DEMO_TAG, "pair status = %s",param->ble_security.auth_cmpl.success ? "success" : "fail");
            if(!param->ble_security.auth_cmpl.success) {
                ESP_LOGE(HID_DEMO_TAG, "fail reason = 0x%x",param->ble_security.auth_cmpl.fail_reason);
            }
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
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;     //bonding with peer device after authentication
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

    keyboard_task();
}