#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#ifdef CONFIG_USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>
#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#endif

#define HASH_LEN 32

#ifdef CONFIG_FIRMWARE_UPGRADE_BIND_IF
/* The interface name value can refer to if_desc in esp_netif_defaults.h */
#if CONFIG_FIRMWARE_UPGRADE_BIND_IF_ETH
static const char *bind_interface_name = EXAMPLE_NETIF_DESC_ETH;
#elif CONFIG_FIRMWARE_UPGRADE_BIND_IF_STA
static const char *bind_interface_name = EXAMPLE_NETIF_DESC_STA;
#endif
#endif

static const char *TAG = "AIoT: AutoEye OTA";

#define OTA_URL_SIZE 256

void ota_task(){
    ESP_LOGI(TAG, "Starting OTA task");
#ifdef CONFIG_FIRMWARE_UPGRADE_BIND_IF
    esp_netif_t *netif = get_example_netif_from_desc(bind_interface_name);
    if (netif == NULL) {
        ESP_LOGE(TAG, "Can't find netif from interface description");
        abort();
    }
    struct ifreq ifr;
    esp_netif_get_netif_impl_name(netif, ifr.ifr_name);
    ESP_LOGI(TAG, "Bind interface name is %s", ifr.ifr_name);
#endif
    esp_http_client_config_t config = {};
    config.url = CONFIG_FIRMWARE_UPGRADE_URL;
    config.buffer_size_tx = 10240; // Set a larger buffer size for OTA
#ifdef CONFIG_USE_CERT_BUNDLE
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.cert_pem = (char *)server_cert_pem_start;
#endif /* CONFIG_EXAMPLE_USE_CERT_BUNDLE */
    config.event_handler = nullptr;
    config.keep_alive_enable = true;
#ifdef CONFIG_FIRMWARE_UPGRADE_BIND_IF
    config.if_name = &ifr;
#endif

#ifdef CONFIG_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);

    if (ota_config.http_config == NULL) {
        ESP_LOGE(TAG, "esp_https_ota: Invalid argument");
        ESP_LOGE(TAG, "Firmware upgrade failed");
        return;
    }

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (https_ota_handle == NULL) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        return;
    }

    // version checking:
    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        return;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    if (memcmp(app_desc.version, running_app_info.version, sizeof(app_desc.version)) == 0) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return;
    }

    // good to go, start OTA
    ESP_LOGI(TAG, "Starting OTA update");
    ESP_LOGI(TAG, "New firmware version: %s", app_desc.version);

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        esp_https_ota_abort(https_ota_handle);
        return;
    }

    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        return;
    }

    ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
    esp_restart();
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void check_for_update() {
    ESP_LOGI(TAG, "Checking for firmware updates...");
    ota_task(); // Start OTA task
}