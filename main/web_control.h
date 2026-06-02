#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_control_start(void);
esp_err_t web_control_stop(void);
bool web_control_is_running(void);

#ifdef __cplusplus
}
#endif
