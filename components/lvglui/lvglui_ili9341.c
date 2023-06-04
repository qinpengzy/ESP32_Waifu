#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#include "lv_demos.h"

#include "lvgl_helpers.h"
#include "esp_freertos_hooks.h"

#include "esp_system.h"
#include "esp_log.h"

#include "lvglui_ili9341.h"
#include "ui/ui.h"

#include "jpeg_decoder.h"

bool ui_updated = false;   // UI 更新标志位

static const char *TAG = "lvgl_ui";

//screen
static void lv_tick_task(void *arg)
{
   (void)arg;
   lv_tick_inc(10);
}

SemaphoreHandle_t xGuiSemaphore;

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    lv_obj_t * kb = lv_event_get_user_data(e);

    if(code == LV_EVENT_FOCUSED) {
        if(lv_indev_get_type(lv_indev_get_act()) != LV_INDEV_TYPE_KEYPAD) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if(code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        lv_indev_reset(NULL, ta);   /*To forget the last clicked object to make it focusable again*/
    }
}

void lv_example_ime_pinyin_1(void)
{
    lv_obj_t * pinyin_ime = lv_ime_pinyin_create(lv_scr_act());
    lv_obj_set_style_text_font(pinyin_ime, &lv_font_simsun_16_cjk, 0);
    //lv_ime_pinyin_set_dict(pinyin_ime, your_dict); // Use a custom dictionary. If it is not set, the built-in dictionary will be used.

    /* ta1 */
    lv_obj_t * ta1 = lv_textarea_create(lv_scr_act());
    lv_textarea_set_one_line(ta1, true);
    lv_obj_set_style_text_font(ta1, &lv_font_simsun_16_cjk, 0);
    lv_obj_align(ta1, LV_ALIGN_TOP_LEFT, 0, 0);

    /*Create a keyboard and add it to ime_pinyin*/
    lv_obj_t * kb = lv_keyboard_create(lv_scr_act());
    lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
    lv_keyboard_set_textarea(kb, ta1);

    lv_obj_add_event_cb(ta1, ta_event_cb, LV_EVENT_ALL, kb);

    /*Get the cand_panel, and adjust its size and position*/
    lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
    lv_obj_set_size(cand_panel, LV_PCT(100), LV_PCT(10));
    lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);

    /*Try using ime_pinyin to output the Chinese below in the ta1 above*/
    lv_obj_t * cz_label = lv_label_create(lv_scr_act());
    lv_label_set_text(cz_label,
                      "嵌入式系统（Embedded System），\n是一种嵌入机械或电气系统内部、具有专一功能和实时计算性能的计算机系统。");
    lv_obj_set_style_text_font(cz_label, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_width(cz_label, 310);
    lv_obj_align_to(cz_label, ta1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
}

void gui_task(void *arg)
{
   xGuiSemaphore = xSemaphoreCreateMutex();
   lv_init();          //lvgl内核初始化
   lvgl_driver_init(); //lvgl显示接口初始化

   /* Example for 1) */
   static lv_disp_draw_buf_t draw_buf;
   
   lv_color_t *buf1 = heap_caps_malloc(DISP_BUF_SIZE * 2, MALLOC_CAP_DMA);
   lv_color_t *buf2 = heap_caps_malloc(DISP_BUF_SIZE * 2, MALLOC_CAP_DMA);

   lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DLV_HOR_RES_MAX * DLV_VER_RES_MAX); /*Initialize the display buffer*/

   static lv_disp_drv_t disp_drv;         /*A variable to hold the drivers. Must be static or global.*/
   lv_disp_drv_init(&disp_drv);           /*Basic initialization*/
   disp_drv.draw_buf = &draw_buf;         /*Set an initialized buffer*/
   disp_drv.flush_cb = disp_driver_flush; /*Set a flush callback to draw to the display*/
   disp_drv.hor_res = 240;                /*Set the horizontal resolution in pixels*/
   disp_drv.ver_res = 320;                /*Set the vertical resolution in pixels*/
   lv_disp_drv_register(&disp_drv);       /*Register the driver and save the created display objects*/
   /*触摸屏输入接口配置*/
	lv_indev_drv_t indev_drv;
	lv_indev_drv_init(&indev_drv);
	indev_drv.read_cb = touch_driver_read;
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	lv_indev_drv_register(&indev_drv);

   // esp_register_freertos_tick_hook(lv_tick_task);
   
	/* 创建一个定时器中断来进入 lv_tick_inc 给lvgl运行提供心跳 这里是10ms一次 主要是动画运行要用到 */
	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &lv_tick_task,
		.name = "periodic_gui"};
	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 10 * 1000));
   ui_init();

   size_t freeHeapSize = esp_get_free_heap_size();
   ESP_LOGI(TAG, "剩余内存大小：%d 字节", freeHeapSize);

   size_t out_img_buf_size = 50000;
   uint8_t *out_img_buf = (uint8_t *)heap_caps_malloc(out_img_buf_size, MALLOC_CAP_DMA);
   if (!out_img_buf) {
      ESP_LOGE(TAG, "Failed to allocate memory for decoded image");
   }

   // Decode the JPEG image
   esp_jpeg_image_output_t outimg;


   while (1)
   {
      // 尝试接收队列中的图片
      if (xQueueReceive(queue, &src, 0) == pdTRUE) {  
         ui_updated = true;     // 设置 UI 更新标志位

         // Set up the JPEG decoder configuration
         esp_jpeg_image_cfg_t jpeg_cfg = {
            .indata = src.image_data,
            .indata_size = src.width * src.height * 2, // You may need to update this value based on the actual JPEG data size
            .outbuf = out_img_buf,
            .outbuf_size = out_img_buf_size,
            .out_format = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale = JPEG_IMAGE_SCALE_0,
            .flags = {
                  .swap_color_bytes = 1,
            }
         };


         int ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
         if (ret != ESP_OK) {
            ESP_LOGE(TAG, "JPEG decoding failed: %d", ret);
         }
         updateVideoImage(out_img_buf, src.width, src.height);
      }

      /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
      vTaskDelay(pdMS_TO_TICKS(10));

      /* Try to take the semaphore, call lvgl related function on success */
      if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
      {
         // 如果标志位设置,重绘屏幕并清除标志位
         if (ui_updated) {
            lv_task_handler();  
            ui_updated = false; 
         } 
         lv_timer_handler();
         xSemaphoreGive(xGuiSemaphore);
      }
   }
}