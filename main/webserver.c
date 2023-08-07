#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "esp_vfs.h"

#include "led_strip_ctl.h"

static const char *TAG = "webserver";

#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context
{
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

// ################################################################### Handler ###################################################################

// Root website Handler
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    ESP_LOGI(TAG, "Serve root");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}

// API Post handler
static esp_err_t api_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    ESP_LOGI(TAG, "received %d size", total_len);
    int cur_len = 0;
    char *buf = malloc(sizeof(char) * total_len); //((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE)
    {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len)
    {
        ESP_LOGI(TAG, "CurLen: %d size", cur_len);
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0)
        {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ESP_LOGI(TAG, "Buf: %s size", buf);
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    int num_elements = cJSON_GetArraySize(root);

    ESP_LOGI(TAG, "Number of elements in the JSON array: %d", num_elements);

    free(led_strip_pixels);

    led_strip_pixels = (uint8_t *)malloc(num_elements * 3);

    if (led_strip_pixels == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed for led_strip_pixels.");
        cJSON_Delete(root);
    }

    for (int i = 0; i < num_elements; i++)
    {
        cJSON *element = cJSON_GetArrayItem(root, i);
        if (element && cJSON_IsObject(element))
        {
            cJSON *id_obj = cJSON_GetObjectItem(element, "id");
            cJSON *r_obj = cJSON_GetObjectItem(element, "r");
            cJSON *g_obj = cJSON_GetObjectItem(element, "g");
            cJSON *b_obj = cJSON_GetObjectItem(element, "b");

            if (id_obj && r_obj && g_obj && b_obj)
            {
                int id = id_obj->valueint;
                int r = r_obj->valueint;
                int g = g_obj->valueint;
                int b = b_obj->valueint;

                led_strip_pixels[id * 3 + 0] = g; // green
                led_strip_pixels[id * 3 + 1] = r; // red
                led_strip_pixels[id * 3 + 2] = b; // blue

                ESP_LOGI(TAG, "Values: id: %i, r: %i, g: %i, b: %i", id, r, g, b);
                
            }
        }
    }

    update_needed = true;

    httpd_resp_sendstr(req, "Post control value successfully");

    cJSON_Delete(root);

    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

// ################################################################### Sites ###################################################################

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler};

static const httpd_uri_t api = {
    .uri = "/api",
    .method = HTTP_POST,
    .handler = api_post_handler};

// ################################################################### Webserver ###################################################################

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &api);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    return server;
}