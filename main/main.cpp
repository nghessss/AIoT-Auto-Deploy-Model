#include "esp_camera.h"

#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "esp_event.h"
#include "esp_netif.h"

#include <sys/param.h>
#include <string.h>

#include "app_wifi.h"
#include "app_mqtt.h"
#include "app_model.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AIoT: AutoEye";

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    app_wifi_main();
    app_mqtt_main();
    init_model();
}