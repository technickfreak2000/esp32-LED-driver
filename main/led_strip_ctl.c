#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#include "cJSON.h"

static const char *TAG = "rmt led strip";

uint8_t* led_strip_pixels_frame = NULL;

TaskHandle_t ledTaskHandle = NULL;


/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i)
    {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

// Process a single frame JSON object and update led_strip_pixels_frame
void process_frame(cJSON *frameJson, uint8_t *led_strip_pixels_frame) {
    cJSON *frameArray = cJSON_GetObjectItem(frameJson, "frame");
    if (frameArray != NULL && cJSON_IsArray(frameArray)) {
        int numPixels = cJSON_GetArraySize(frameArray);
        for (int i = 0; i < numPixels; i++) {
            cJSON *pixelJson = cJSON_GetArrayItem(frameArray, i);
            if (pixelJson != NULL && cJSON_IsObject(pixelJson)) {
                cJSON *r_obj = cJSON_GetObjectItem(pixelJson, "r");
                cJSON *g_obj = cJSON_GetObjectItem(pixelJson, "g");
                cJSON *b_obj = cJSON_GetObjectItem(pixelJson, "b");

                if (r_obj && g_obj && b_obj) {
                    int r = r_obj->valueint;
                    int g = g_obj->valueint;
                    int b = b_obj->valueint;

                    // Update the led_strip_pixels_frame for the current pixel
                    int pixelIndex = i * 3;
                    led_strip_pixels_frame[pixelIndex] = g; // green
                    led_strip_pixels_frame[pixelIndex + 1] = r; // red
                    led_strip_pixels_frame[pixelIndex + 2] = b; // blue
                }
            }
        }
    }
}

void task_led_strip(void *arg)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = CONFIG_ESP_LED_PIN,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = CONFIG_RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = CONFIG_RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start LED task");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };

    uint8_t* led_strip_pixels_frame = NULL;
    while (1) {
        // Create a buffer for JSON chunk parsing
        char chunk[128*10];

        FILE *f = fopen("/data/data.json", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file");
            vTaskDelete(NULL);
        }

        cJSON *frameJson = NULL;
        size_t bytesRead;
        int frameRate = 60; // Default frame rate

        while (!feof(f)) {
            bytesRead = fread(chunk, 1, sizeof(chunk), f);
            if (bytesRead > 0) {
                chunk[bytesRead] = '\0';

                cJSON *partialJson = cJSON_Parse(chunk);
                if (partialJson != NULL) {
                    cJSON *fps_obj = cJSON_GetObjectItem(partialJson, "fps");
                    if (fps_obj && cJSON_IsNumber(fps_obj)) {
                        frameRate = fps_obj->valueint;
                        ESP_LOGI(TAG, "Got frame rate: %d", frameRate);
                    }

                    cJSON *dataArray = cJSON_GetObjectItem(partialJson, "data");
                    if (dataArray != NULL && cJSON_IsArray(dataArray)) {
                        int numFrames = cJSON_GetArraySize(dataArray);
                        for (int i = 0; i < numFrames; i++) {
                            frameJson = cJSON_GetArrayItem(dataArray, i);
                            if (frameJson != NULL && cJSON_IsObject(frameJson)) {
                                process_frame(frameJson, led_strip_pixels_frame);
                                // Update the LED strip with led_strip_pixels_frame
                                ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels_frame, (sizeof(led_strip_pixels_frame) * 3), &tx_config));

                                // Calculate delay between frames based on frame rate
                                vTaskDelay(pdMS_TO_TICKS(1000 / frameRate));
                            }
                        }
                    }
                    cJSON_Delete(partialJson);
                } else {
                    ESP_LOGE(TAG, "Failed to parse JSON chunk: %s", cJSON_GetErrorPtr());
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                }
            }
        }

        fclose(f);
    }
}

void init_led_strip(void)
{
    xTaskCreatePinnedToCore(task_led_strip, "task_led_strip", 4096, NULL, 10, &ledTaskHandle, 1);
}
