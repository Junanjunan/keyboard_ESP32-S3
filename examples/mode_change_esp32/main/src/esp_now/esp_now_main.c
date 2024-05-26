#include <stdio.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "hid.h"

#define ESP_CHANNEL         1


static uint8_t peer_mac [ESP_NOW_ETH_ALEN] = {0x24, 0x58, 0x7C, 0xCD, 0x93, 0x30};


static const char * TAG = "esp_now_init";


static esp_err_t init_wifi(void)
{
    wifi_init_config_t wifi_init_config =  WIFI_INIT_CONFIG_DEFAULT();
    esp_netif_init();
    esp_event_loop_create_default();
    nvs_flash_init();
    esp_wifi_init(&wifi_init_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_wifi_start();
    ESP_LOGI(TAG, "wifi init completed");
    return ESP_OK;
}


void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if(status == ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGI(TAG, "ESP_NOW_SEND_SUCCESS");
    }
    else
    {
        ESP_LOGE(TAG, "ESP_NOW_SEND_FAIL");
    }
}


void recv_cb(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len)
{
    uint8_t key[6] = {0};
    uint8_t converted_key = atoi((const char *)data);
    key[0] = converted_key;

    ESP_LOGI(__func__, "Received key: %d", converted_key);
}


static esp_err_t init_esp_now(void)
{
    esp_now_init();
    esp_now_register_send_cb(send_cb);
    esp_now_register_recv_cb(recv_cb);

    ESP_LOGI(TAG, "esp now init completed");
    return ESP_OK;
}


static esp_err_t register_peer(uint8_t *peer_addr)
{
    esp_now_peer_info_t esp_now_peer_info = {};
    memcpy(esp_now_peer_info.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    esp_now_peer_info.channel = ESP_CHANNEL;
    esp_now_peer_info.ifidx = ESP_IF_WIFI_STA;
    esp_now_add_peer(&esp_now_peer_info);
    return ESP_OK;
}


void esp_now_main(void)
{
    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(init_esp_now());
    ESP_ERROR_CHECK(register_peer(peer_mac));
}
