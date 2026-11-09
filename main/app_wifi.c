#include "app_wifi.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

esp_err_t app_wifi_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap) return ESP_ERR_NO_MEM;

    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = ESP_IP4TOADDR(192, 168, 8, 1);
    ip.gw = ip.ip;
    ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    err = esp_netif_dhcps_stop(ap);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(ap, &ip);
    if (err != ESP_OK) return err;
    err = esp_netif_dhcps_start(ap);
    if (err != ESP_OK) return err;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    wifi_config_t config = {.ap = {
        .ssid = "NEKTARINA-S31",
        .channel = 1, .max_connection = 4,
        .authmode = WIFI_AUTH_WPA2_PSK, .pmf_cfg = {.capable = true},
    }};
    size_t password_length = strlen(CONFIG_NEKTAR_AP_PASSWORD);
    if (password_length < 8 || password_length > 63) {
        (void)esp_wifi_deinit();
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config.ap.password, CONFIG_NEKTAR_AP_PASSWORD, password_length);
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) == ESP_OK &&
        (err = esp_wifi_set_mode(WIFI_MODE_AP)) == ESP_OK &&
        (err = esp_wifi_set_config(WIFI_IF_AP, &config)) == ESP_OK) {
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            err = esp_wifi_start();
            if (err == ESP_OK) {
                ESP_LOGI("S31_WIFI", "SSID=%s URL=http://" NEKTAR_AP_IP "/",
                         (const char *)config.ap.ssid);
                return ESP_OK;
            }
            if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    (void)esp_wifi_deinit();
    return err;
}
