

#include "lv_demos.h"
#include "lvgl_helpers.h"
#include "esp_freertos_hooks.h"
#include "freertos/queue.h"


void gui_task(void *arg);

typedef struct {
    unsigned char *image_data;
    int width;
    int height;
} image_data_t;

image_data_t src;

// Define a global variable to store the queue handle
QueueHandle_t queue;