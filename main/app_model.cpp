#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_log.h"

#include <functional>
#include <cstdint>

#include "app_model.h"
#include "app_camera.h"
#include "esp_timer.h"
#include "edge-impulse-sdk/dsp/image/image.hpp"
// #include "encode_as_jpg.h"
#include "at_base64_lib.h"
#include "app_mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

// debug
#include "esp_heap_caps.h"
#include "stream_server.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_enc.h"
#include "esp_sntp.h"

#define EDGE_LOCATION ""
#define EDGE_UNIT_ID ""

static const char* TAG = "AIoT: AutoEye";

typedef enum {
    INFERENCE_STOPPED,
    INFERENCE_WAITING,
    INFERENCE_SAMPLING,
    INFERENCE_DATA_READY
} inference_state_t;

static inference_state_t state = INFERENCE_STOPPED;
static uint64_t last_inference_ts = 0;

typedef struct {
    uint16_t width;
    uint16_t height;
} ei_device_snapshot_resolutions_t;

static uint8_t *snapshot_buf = nullptr;
static uint32_t snapshot_buf_size;

static ei_device_snapshot_resolutions_t snapshot_resolution;
static ei_device_snapshot_resolutions_t fb_resolution;

static bool debug_mode = false;
static bool resize_required = false;
static bool continuous_mode = false;
static uint32_t inference_delay;

static float confidence_level = 0.5;

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    // we already have a RGB888 buffer, so recalculate offset into pixel index
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix + 2];

        // go to the next pixel
        out_ptr_ix++;
        pixel_ix+=3;
        pixels_left--;
    }

    // and done!
    return 0;
}

void init_model() {
    ESPCamModel* cam = ESPCamModel::get_camera();

    snapshot_resolution.width = EI_CLASSIFIER_INPUT_WIDTH;
    snapshot_resolution.height = EI_CLASSIFIER_INPUT_HEIGHT;
    fb_resolution.width = 240;
    fb_resolution.height = 176;

    if(!cam->init()) {
        ESP_LOGE(TAG, "Failed to init camera, check if camera is connected!");
    }

    snapshot_buf_size = fb_resolution.width * fb_resolution.height * 3;
    ESP_LOGI(TAG, "Inferencing settings:");
    ESP_LOGI(TAG, "Image resolution: %dx%d", EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    ESP_LOGI(TAG, "Frame size: %d", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ESP_LOGI(TAG, "No. of classes: %d", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    inference_delay = 50;
    last_inference_ts = ei_read_timer_ms();
    state = INFERENCE_WAITING;
    ei_printf("Starting inferencing in %d seconds...\n", inference_delay / 1000);

    while(true) {
        run_model();
        ei_sleep(1);
    }
}

esp_err_t run_model() {

    switch(state) {
        case INFERENCE_STOPPED:
            // nothing to do
            return ESP_OK;
        case INFERENCE_WAITING: {
            if(ei_read_timer_ms() < (last_inference_ts + inference_delay)) {
                return ESP_OK;
            }
            state = INFERENCE_DATA_READY;
            break;
        }
        case INFERENCE_SAMPLING:
        case INFERENCE_DATA_READY:
            if(continuous_mode == true) {
                state = INFERENCE_WAITING;
            }
            break;
        default:
            break;
    }

    ei_printf("Edge Impulse standalone inferencing (Espressif ESP32)\n");
    
    uint8_t *jpeg_img = nullptr;
    uint32_t jpeg_img_size = 0;

    ESPCamModel *camera = ESPCamModel::get_camera();

    ei_printf("Taking photo...\n");

    if(camera->camera_capture_jpeg(&jpeg_img, &jpeg_img_size, nullptr) == false) {
        ei_printf("ERR: Failed to take a snapshot!\n");
        return ESP_FAIL;
    }

    snapshot_buf = (uint8_t*)ei_malloc(snapshot_buf_size);

    // check if allocation was successful
    if(snapshot_buf == nullptr) {
        ei_printf("ERR: Failed to allocate snapshot buffer!\n");
        return ESP_FAIL;
    }

    // no need to do anything here
    if(camera->to_rgb888(jpeg_img, jpeg_img_size, PIXFORMAT_JPEG, snapshot_buf) == false) {
        ei_printf("ERR: Failed to decode image\n");
        ei_free(snapshot_buf);
        ei_free(jpeg_img);
        return ESP_FAIL;
    }
    ei_free(jpeg_img);
    jpeg_img_size = 0;

    int64_t fr_start = esp_timer_get_time();

    // resize
    ei::image::processing::crop_and_interpolate_rgb888(
        snapshot_buf,
        fb_resolution.width,
        fb_resolution.height,
        snapshot_buf,
        snapshot_resolution.width,
        snapshot_resolution.height);

    int64_t fr_end = esp_timer_get_time();

    if (debug_mode) {
        ei_printf("Time resizing: %d\n", (uint32_t)((fr_end - fr_start)/1000));
    }

    // encode as base64

    // configure encoder
    jpeg_enc_config_t jpeg_enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
    jpeg_enc_cfg.width = snapshot_resolution.width;
    jpeg_enc_cfg.height = snapshot_resolution.height;
    jpeg_enc_cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_enc_cfg.subsampling = JPEG_SUBSAMPLE_420;
    jpeg_enc_cfg.quality = 90;
    jpeg_enc_cfg.rotate = JPEG_ROTATE_0D;
    jpeg_enc_cfg.task_enable = false;
    jpeg_enc_cfg.hfm_task_priority = 13;
    jpeg_enc_cfg.hfm_task_core = 1;

    jpeg_error_t ret = JPEG_ERR_OK;
    uint8_t *inbuf = snapshot_buf;
    int image_size = snapshot_resolution.width * snapshot_resolution.height * 3;
    uint8_t *outbuf = NULL;
    int outbuf_size = 100 * 1024;
    int out_len = 0;
    jpeg_enc_handle_t jpeg_enc = NULL;

    // open
    ret = jpeg_enc_open(&jpeg_enc_cfg, &jpeg_enc);
    if (ret != JPEG_ERR_OK) {
        return ret;
    }

    // allocate output buffer to fill encoded image stream
    outbuf = (uint8_t *)calloc(1, outbuf_size);
    if (outbuf == NULL) {
        ret = JPEG_ERR_NO_MEM;
        return ESP_FAIL;
    }

    // process
    ret = jpeg_enc_process(jpeg_enc, inbuf, image_size, outbuf, outbuf_size, &out_len);
    if (ret != JPEG_ERR_OK) {
        return ESP_FAIL;
    }

    // close
    jpeg_enc_close(jpeg_enc);

    uint8_t* base64_img_out = nullptr;
    base64_img_out = (uint8_t*)ei_malloc(4 * ((out_len + 2) / 3) + 1);
    int base_64_img_out_len  = base64_encode_buffer((const char*)outbuf, out_len, (char*)base64_img_out, 4 * ((out_len + 2) / 3) + 1);

    if(base_64_img_out_len < 0) {
        ei_printf("ERR: Failed to encode frame as base64 (%d)\n", base_64_img_out_len);
        ei_free(base64_img_out);
        base64_img_out = nullptr;
    }
    if(outbuf) {
        free(outbuf);
    }
    out_len = 0;

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    // run the impulse: DSP, neural network and the Anomaly algorithm
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR ei_error = run_classifier(&signal, &result, false);
    if (ei_error != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run impulse (%d)\n", ei_error);
        ei_free(snapshot_buf);
        return ESP_FAIL;
    }
    ei_free(snapshot_buf);

    display_results(&ei_default_impulse, &result);

    int** bbox_list = (int**)ei_malloc(result.bounding_boxes_count*(sizeof(int*)));

    for (size_t ix = 0; ix < result.bounding_boxes_count; ix++) {
        bbox_list[ix] = (int*)ei_malloc(6*(sizeof(int)));
        auto bb = result.bounding_boxes[ix];
        if (bb.value < confidence_level) {
            continue;
        }
        bbox_list[ix][0] = (strcmp(bb.label, "car") == 0 ? 0 : 1);
        // rescale so that it matches the original image coordinate
        bbox_list[ix][1] = bb.x;
        bbox_list[ix][2] = bb.y;
        bbox_list[ix][3] = bb.width;
        bbox_list[ix][4] = bb.height;
        bbox_list[ix][5] = (bb.x > (160/2) ? 0 : 1);
    }

    MQTTMessage msg{};
    msg.edge_id = EDGE_UNIT_ID;
    msg.edge_id_len = sizeof(EDGE_UNIT_ID);
    msg.base64_buffer = base64_img_out;
    msg.base64_buffer_len = base_64_img_out_len;
    msg.location = EDGE_LOCATION;
    msg.location_len = sizeof(EDGE_LOCATION);
    msg.bbox = bbox_list;
    msg.bbox_len = result.bounding_boxes_count;

    if(is_mqtt_connected()) {
        publish_message(msg);
    }
    if(base64_img_out != nullptr) {
        ei_free(base64_img_out);
        base64_img_out = nullptr;
    }

    for(size_t ix = 0; ix < result.bounding_boxes_count; ix++) {
        ei_free(bbox_list[ix]);
    }
    ei_free(bbox_list);

    if (debug_mode) {
        ei_printf("\r\n----------------------------------\r\n");
        ei_printf("End output\r\n");
    }
    last_inference_ts = ei_read_timer_ms();
    state = INFERENCE_WAITING;

    if(continuous_mode == false) {
        ei_printf("Starting inferencing in %d seconds...\n", inference_delay / 1000);
    }
    
    return ESP_OK;
}