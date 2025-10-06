#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "led_strip.h"
#include "esp_log.h"
#include "mdns.h"

// Define your WIFI_SSID and WIFI_PASSWORD in a seperate header
#include "wifi-creds.h"

#define LED_GPIO    5
#define LED_COUNT   256  // 16x16

static const char *TAG = "led_server";
static led_strip_handle_t strip;
static SemaphoreHandle_t led_mutex = NULL;

// embedded html file
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// root html page
static esp_err_t root_handler(httpd_req_t *req) {
    const size_t html_len = index_html_end - index_html_start;
    httpd_resp_send(req, (const char *)index_html_start, html_len);
    return ESP_OK;
}

// POST handler - set pixel
// format: {"index":0,"r":255,"g":0,"b":0}
static esp_err_t set_pixel_handler(httpd_req_t *req) {
  char buf[100];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  buf[ret] = '\0';
    
  int index = 0, r = 0, g = 0, b = 0;
  sscanf(buf, "{\"index\":%d,\"r\":%d,\"g\":%d,\"b\":%d}", &index, &r, &g, &b);
    
  if (index >= 0 && index < LED_COUNT) {
    if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      led_strip_set_pixel(strip, index, r, g, b);
      led_strip_refresh(strip);
      xSemaphoreGive(led_mutex);
      httpd_resp_send(req, "OK", 2);
    } else {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "LED busy");
    }
  }
  return ESP_OK;
}

static esp_err_t off_handler(httpd_req_t *req) {
  led_strip_clear(strip);
  led_strip_refresh(strip);
  httpd_resp_send(req, "OFF", 3);
  return ESP_OK;
}

// start http server
static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_open_sockets = 7;
  config.lru_purge_enable = true;
  config.stack_size = 8192;
  config.task_priority = 5;
  config.keep_alive_enable = true;
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_handler,
    };
    httpd_register_uri_handler(server, &root_uri);
        
    httpd_uri_t set_pixel_uri = {
      .uri = "/pixel",
      .method = HTTP_POST,
      .handler = set_pixel_handler,
    };
    httpd_register_uri_handler(server, &set_pixel_uri);

    httpd_uri_t off_uri = {
      .uri = "/off",
      .method = HTTP_POST,
      .handler = off_handler,
    };
    httpd_register_uri_handler(server, &off_uri);
  }
  
  return server;
}

// wifi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected, retrying...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    
    // start mdns
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("matrix"));
    ESP_ERROR_CHECK(mdns_instance_name_set("LED Matrix"));
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started, hostname: matrix.local");
    
    // start server after connection is established
    start_webserver();
  }
}

// init wifi
static void init_wifi(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
    
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
  wifi_config_t wifi_config = {
    .sta = {
      .ssid = WIFI_SSID,
      .password = WIFI_PASSWORD,
    },
  };
    
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
    
  ESP_LOGI(TAG, "Connecting to WiFi...");
}

void app_main(void) {
    // init led strip
    led_strip_config_t config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10000000,
    };
    led_strip_new_rmt_device(&config, &rmt_config, &strip);
    led_mutex = xSemaphoreCreateMutex();
    
    led_strip_clear(strip);
    led_strip_refresh(strip);
    
    ESP_LOGI(TAG, "Starting WiFi...");
    init_wifi();
}
