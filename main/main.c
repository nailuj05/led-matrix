#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

#define LED_GPIO    5
#define LED_COUNT   256  // 16x16

static led_strip_handle_t strip;

int first, trail;
int length = 16;

void app_main(void) {
  // Init LED strip
  led_strip_config_t config = {
    .strip_gpio_num = LED_GPIO,
    .max_leds = LED_COUNT,
  };
  led_strip_rmt_config_t rmt_config = {
    .resolution_hz = 10000000,
  };
  led_strip_new_rmt_device(&config, &rmt_config, &strip);

  first = length;
  
  while (1) {
    led_strip_clear(strip);
    
    led_strip_set_pixel(strip, first, 10, 0, 0);
    
    for(int i = 1; i < length; i++) {
      trail = (first - i + LED_COUNT) % LED_COUNT;
      int brightness = 10 * (length - i) / length;
      led_strip_set_pixel(strip, trail, brightness, 0, 0);
    }
    
    led_strip_refresh(strip);
    first = (first + 1) % LED_COUNT;
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}
