#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h> 

#include "lvglui_ili9341.h"
#include "freertos/queue.h"

#define PORT 8080
#define RECV_BUFFER_SIZE 40960

static const char *TAG = "video_receiver";

void video_recv_task(void *pvParameters)
{
    int sock = -1;
    uint16_t length;
    struct sockaddr_in addr;
    uint8_t *frame_buffer = malloc(RECV_BUFFER_SIZE);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ESP_LOGE(TAG, "Socket error");
        return;
    } 

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Bind error");
        return;
    }

    if (listen(sock, 5) < 0) {
        ESP_LOGE(TAG, "Listen error");
        return;
    }

    while (1) {
        struct sockaddr_storage source_addr; 
        socklen_t addr_len = sizeof(source_addr);
        int client_sock = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection");
            continue;
        }

        ESP_LOGI(TAG, "New client connection"); 

        while (1) {  
            // Receive frame length
            int len_recv = recv(client_sock, (char *)&length, 4, 0);
            if (len_recv <= 0) break;
            ESP_LOGI(TAG, "Receive frame length: %d", length);

            // // Receive frame data
            // len_recv = recv(client_sock, (char *)frame_buffer, length, 0);
            // if (len_recv <= 0) break;           
            // ESP_LOGI(TAG, "Receive frame data %d bytes", len_recv);

            // Receive complete frame data
            uint16_t data_len = 0;
            while (data_len < length) {
                len_recv = recv(client_sock, (char *)frame_buffer + data_len, length - data_len, 0);
                if (len_recv <= 0) break;
                data_len += len_recv;
            }
            ESP_LOGI(TAG, "Receive complete frame data %d bytes", data_len);

            // Decode frame_buffer into a suitable format for LVGL (e.g. GB565)
            // Assuming the function 'decode_frame' is implemented to handle this task
            // int frame_width = ...; // Get decoded frame width
            // int frame_height = ...; // Get decoded frame height
            // decode_frame(frame_buffer, decoded_frame_buffer, &frame_width, &frame_height);

            // Fill the src struct with the decoded frame data, width, and height
            image_data_t src;
            src.image_data = frame_buffer; // Replace 'frame_buffer' with 'decoded_frame_buffer' if you use a separate buffer for the decoded frame
            src.width = 240; //
            src.height = 180;

            // Send the src struct to the queue
            if (xQueueSend(queue, &src, 0) != pdPASS) {
                ESP_LOGE(TAG, "Error sending frame to queue");
            }
        }

        ESP_LOGI(TAG, "Client disonnected");
        close(client_sock);
    }
}

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", CONFIG_WIFI_SSID, "******");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}



void app_main(void)
{
    wifi_init(); // Wifi initialization

    queue = xQueueCreate(10, sizeof(image_data_t));
    if (queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
    }
    
    xTaskCreate(video_recv_task, "video_recv_task", 8192, NULL, 5, NULL);
    xTaskCreate(gui_task, "gui task", 4096, NULL, 1, NULL);
    
}