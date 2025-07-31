#ifndef _APP_CAMERA_H
#define _APP_CAMERA_H

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class ESPCamModel {
public:
    bool camera_capture_jpeg(uint8_t **image, uint32_t *image_size, QueueHandle_t frameOut);
    esp_err_t init();
    static ESPCamModel* get_camera();
    bool to_rgb888(uint8_t *jpeg_image, uint32_t jpeg_image_size, pixformat_t format, uint8_t *rgb88_image);
};

#endif