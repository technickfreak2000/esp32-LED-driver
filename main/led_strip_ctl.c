#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#include "cJSON.h"

static const char *TAG = "rmt led strip";

uint8_t* led_strip_pixels_frame = NULL;
char* received_buffer = NULL;
char* received_buffer_helper = NULL;

TaskHandle_t ledTaskHandle = NULL;


void detect_leds(size_t *led_count, char *received_buffer)
{
    // go through received buffer and count characters until NULL or \0
    // divide counted characters two times by 3 
    // malloc led_strip_pixels_frame
}

void push_frame(char *received_buffer)
{
    /* go through received buffer until NULL or \0
    Write each character to corresponding thing in led_strip_pixels_frame like:
    int pixelIndex = i * 3;
    led_strip_pixels_frame[pixelIndex] = g; // green
    led_strip_pixels_frame[pixelIndex + 1] = r; // red
    led_strip_pixels_frame[pixelIndex + 2] = b; // blue
    Update LEDs like: 
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, (sizeof(led_strip_pixels)*3), &tx_config));
    Wait for frame rate: 
    vTaskDelay(pdMS_TO_TICKS(1000 / frameRate));
    */
}

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

    while (1) {
        FILE *f = fopen("/data/data.rgb", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file");
            vTaskDelete(NULL);
        }

        char buffer[128];
        size_t bytesRead;
        size_t detected = 0;
        uint8_t frameRate = 1; // Default frame rate
        size_t received_buffer_size_default = 1000;
        size_t received_buffer_size = received_buffer_size_default;
        size_t chunkRed = 0;
        size_t led_count = 0;
        free(received_buffer);
        free(received_buffer_helper);
        received_buffer = malloc((received_buffer_size + 1) * sizeof(char));
        received_buffer[received_buffer_size] = '\0';

        while (!feof(f)) {
            bytesRead = fread(buffer, 1, sizeof(buffer), f);
            buffer[bytesRead] = '\0';
            

            if(strcmp(buffer, ";"))
            {
                switch (detected)
                {
                case 0:
                    ESP_LOGW(TAG, "DETECTED FPS, bytesred %d", bytesRead);
                    frameRate = (uint8_t)received_buffer;
                    free(received_buffer);
                    received_buffer_size = received_buffer_size_default;
                    received_buffer = malloc((received_buffer_size + 1) * sizeof(char));
                    chunkRed = bytesRead;
                    break;
                
                default:
                    ESP_LOGW(TAG, "DETECTED FRAME, bytesred %d", bytesRead);
                    if (led_count == 0)
                    {
                        detect_leds(&led_count, &received_buffer);
                    }
                    push_frame(&received_buffer);
                    free(received_buffer);
                    received_buffer_size = received_buffer_size_default;
                    received_buffer = malloc((received_buffer_size + 1) * sizeof(char));
                    chunkRed = bytesRead;
                    break;
                }
                detected++;
            }
            else
            {
                if ((bytesRead+1 - chunkRed) >= received_buffer_size)
                {
                    received_buffer_size += received_buffer_size_default;
                    received_buffer_helper = malloc((received_buffer_size + 1) * sizeof(char));
                    strcpy(received_buffer_helper, received_buffer);
                    free(received_buffer);
                    received_buffer = received_buffer_helper;
                }
                received_buffer[bytesRead-1] = buffer[0];
            }
        }
        free(received_buffer);
        free(received_buffer_helper);
        fclose(f);
    }
}

void init_led_strip(void)
{
    xTaskCreatePinnedToCore(task_led_strip, "task_led_strip", 4096, NULL, 10, &ledTaskHandle, 1);
}
