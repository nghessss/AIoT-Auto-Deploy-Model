
#ifndef _APP_MQTT_H_
#define _APP_MQTT_H_

#include <cstdint>

struct MQTTMessage {
    char* edge_id;
    size_t edge_id_len;
    char* location;
    size_t location_len;

    uint8_t *base64_buffer;
    size_t base64_buffer_len;
    int** bbox;
    size_t bbox_len;
};

void app_mqtt_main();
esp_err_t publish_message(const MQTTMessage &msg);
bool is_mqtt_connected();

#endif