#include "esp_camera.h"
#include "app_camera.h"

#include "img_converters.h"
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_log.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAM_PIN_PWDN    -1 //power down is not used
#define CAM_PIN_RESET   -1 //software reset will be performed
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5

#define CAM_PIN_D7      16
#define CAM_PIN_D6      17
#define CAM_PIN_D5      18
#define CAM_PIN_D4      12
#define CAM_PIN_D3      10
#define CAM_PIN_D2      8
#define CAM_PIN_D1       9
#define CAM_PIN_D0       11
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK    13

const static char* TAG = "AIoT: AutoEye";

esp_err_t ESPCamModel::init() {
    static camera_config_t camera_config = {};

    camera_config.pin_pwdn  = CAM_PIN_PWDN;
    camera_config.pin_reset = CAM_PIN_RESET;
    camera_config.pin_xclk = CAM_PIN_XCLK;
    camera_config.pin_sccb_sda = CAM_PIN_SIOD;
    camera_config.pin_sccb_scl = CAM_PIN_SIOC;

    camera_config.pin_d7 = CAM_PIN_D7;
    camera_config.pin_d6 = CAM_PIN_D6;
    camera_config.pin_d5 = CAM_PIN_D5;
    camera_config.pin_d4 = CAM_PIN_D4;
    camera_config.pin_d3 = CAM_PIN_D3;
    camera_config.pin_d2 = CAM_PIN_D2;
    camera_config.pin_d1 = CAM_PIN_D1;
    camera_config.pin_d0 = CAM_PIN_D0;
    camera_config.pin_vsync = CAM_PIN_VSYNC;
    camera_config.pin_href = CAM_PIN_HREF;
    camera_config.pin_pclk = CAM_PIN_PCLK;

    camera_config.xclk_freq_hz = 20000000;
    camera_config.ledc_timer = LEDC_TIMER_0;
    camera_config.ledc_channel = LEDC_CHANNEL_0;

    camera_config.pixel_format = PIXFORMAT_JPEG;//YUV422,GRAYSCALE,RGB565,JPEG
    camera_config.frame_size = FRAMESIZE_HQVGA;//QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    camera_config.jpeg_quality = 10; //0-63, for OV series camera sensors, lower number means higher quality
    camera_config.fb_count = 2; //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    camera_config.grab_mode = CAMERA_GRAB_LATEST; //CAMERA_GRAB_LATEST. Sets when buffers should be filled
    camera_config.fb_location = CAMERA_FB_IN_PSRAM;
    //initialize the camera

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    s->set_awb_gain(s, 1);

    // camera warm-up to avoid wrong WB
    ei_sleep(10);
    for (uint8_t i = 0; i < 7; i++) {
        camera_fb_t *fb = esp_camera_fb_get();

        if (!fb) {
            ei_printf("ERR: Camera capture failed during warm-up \n");
            return false;
        }
        esp_camera_fb_return(fb);
    }

    return ESP_OK;
}

bool ESPCamModel::camera_capture_jpeg(uint8_t **image, uint32_t *image_size, QueueHandle_t frameOut) {
    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
        ei_printf("ERR: Camera capture failed\n");
        return false;
    }

    ESP_LOGD(TAG, "fb res %d %d \n", fb->width, fb->height);

    *image = nullptr;
    *image = (uint8_t*)ei_malloc(fb->len);

    memcpy(*image, fb->buf, fb->len);
    memcpy(image_size, &fb->len, sizeof(uint32_t));

    if(frameOut != nullptr) {
        xQueueSend(frameOut, &fb, portMAX_DELAY);
    } else {
        esp_camera_fb_return(fb);
    }
    return true;
}

bool ESPCamModel::to_rgb888(uint8_t *jpeg_image, uint32_t jpeg_image_size, pixformat_t format, uint8_t *rgb88_image) {
    bool converted = fmt2rgb888(jpeg_image, jpeg_image_size, format, rgb88_image);

    if(!converted){
        ESP_LOGE(TAG, "ERR: Conversion failed");
        return false;
    }
    return true;
}

ESPCamModel* ESPCamModel::get_camera() {
    static ESPCamModel cam;
    return &cam;
}