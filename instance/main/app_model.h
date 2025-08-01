
#ifndef _APP_MODEL_H_
#define _APP_MODEL_H_

#include <cstdint>
#include "esp_camera.h"

void init_model();
esp_err_t run_model();
#endif