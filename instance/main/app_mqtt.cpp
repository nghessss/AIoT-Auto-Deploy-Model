#include "esp_camera.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "app_mqtt.h"
#include "cJSON.h"

static const char *TAG = "AIoT: AutoEye";
static const char *MQTT_BROKER_URI = "mqtt://192.168.0.41";
static const int MQTT_BROKER_PORT = 1883;
static const char *MQTT_TOPIC = "aiot/data";
static const char *MQTT_USERNAME = "aiot";
static const char *MQTT_PASSWORD = "aiot";
static bool mqtt_connected = false;

static esp_mqtt_client_handle_t client = nullptr;

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        break;
    }
}

bool is_mqtt_connected() {
    return mqtt_connected;
}

esp_err_t publish_message(const MQTTMessage &msg) {
    cJSON *root = cJSON_CreateObject();
    char* output_img = "";
    if(msg.base64_buffer != nullptr) {
        output_img = (char*)malloc(msg.base64_buffer_len+1);
        if(output_img == nullptr) {
            ESP_LOGE(TAG, "Cannot allocate buffer for null-terminated base64 buffer");
            return ESP_FAIL;
        }
        output_img[msg.base64_buffer_len] = '\0';
        memcpy(output_img, msg.base64_buffer, msg.base64_buffer_len);
    }
    
    char *edge_id = "";
    if(msg.edge_id != nullptr) {
        edge_id = (char*)malloc(msg.edge_id_len+1);
        if(edge_id == nullptr) {
            ESP_LOGE(TAG, "Cannot allocate buffer for null-terminated edge id buffer");
            return ESP_FAIL;
        }
        edge_id[msg.edge_id_len] = '\0';
        memcpy(edge_id, msg.edge_id, msg.edge_id_len);
    }
    
    char *location = "";
    if(msg.location != nullptr) {
        location = (char*)malloc(msg.location_len+1);
        if(location == nullptr) {
            ESP_LOGE(TAG, "Cannot allocate buffer for null-terminated location buffer");
            return ESP_FAIL;
        }
        location[msg.location_len] = '\0';
        memcpy(location, msg.location, msg.location_len);
    }

    cJSON_AddStringToObject(root, "image", output_img);
    cJSON_AddStringToObject(root, "edge_id", edge_id);
    cJSON_AddStringToObject(root, "location", location);

    // time
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    time(&now);

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", strftime_buf);
    cJSON_AddStringToObject(root, "timestamp", strftime_buf);
    
    cJSON* bbox_list = cJSON_AddArrayToObject(root, "bbox");
    for(size_t i = 0; i < msg.bbox_len; i++) {
        cJSON* bbox_info = cJSON_CreateObject();
        cJSON_AddStringToObject(bbox_info, "class", msg.bbox[i][0] == 0 ? "car" : "motorbike");
        cJSON_AddNumberToObject(bbox_info, "x", msg.bbox[i][1]);
        cJSON_AddNumberToObject(bbox_info, "y", msg.bbox[i][2]);
        cJSON_AddNumberToObject(bbox_info, "w", msg.bbox[i][3]);
        cJSON_AddNumberToObject(bbox_info, "h", msg.bbox[i][4]);
        cJSON_AddStringToObject(bbox_info, "lane", msg.bbox[i][5] == 0 ? "in" : "out");
        cJSON_AddItemToArray(bbox_list, bbox_info);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, MQTT_TOPIC, json_str, 0, 1, 0);

    cJSON_Delete(root);
    free(json_str);
    if(msg.base64_buffer != nullptr) {
        free(output_img);
    }
    if(msg.edge_id != nullptr) {
        free(edge_id);
    }
    if(msg.location != nullptr) {
        free(location);
    }
    return ESP_OK;
}

void app_mqtt_main() {

    // Set timezone to China Standard Time
    setenv("TZ", "WIB-7", 1);

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    mqtt_cfg.broker.address.port = MQTT_BROKER_PORT;
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}