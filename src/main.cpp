#include <cstring>

#include "app.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "nvs_flash.h"
#include "secrets.h"
#include "web.h"

// Free on ESP32-S3 N16R8 (avoids octal PSRAM 35-37, strapping and USB pins).
constexpr gpio_num_t kPinRun = GPIO_NUM_4;    // drive Run relay contact to GND
constexpr gpio_num_t kPinAlarm = GPIO_NUM_5;  // drive Alarm relay contact to GND
constexpr gpio_num_t kPinAux = GPIO_NUM_6;    // relay module: high closes +24V to Aux In (run allowed)
constexpr int kPinLed = 48;                   // on-board WS2812

static const char* TAG = "main";
static App app;

static void onWifiEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(TAG, "wifi disconnected, reason %d", ev->reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "wifi connected, https://" IPSTR "/", IP2STR(&ev->ip_info.ip));
    }
}

static void startWifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, nullptr));
    wifi_config_t cfg = {};
    strlcpy((char*)cfg.sta.ssid, WIFI_SSID, sizeof cfg.sta.ssid);
    strlcpy((char*)cfg.sta.password, WIFI_PASS, sizeof cfg.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void initGpio() {
    // Open the Aux relay before anything else so the pump stays stopped until the controller decides.
    gpio_config_t out = {};
    out.pin_bit_mask = 1ULL << kPinAux;
    out.mode = GPIO_MODE_OUTPUT;
    gpio_set_level(kPinAux, 0);
    gpio_config(&out);
    gpio_set_level(kPinAux, 0);

    gpio_config_t in = {};
    in.pin_bit_mask = (1ULL << kPinRun) | (1ULL << kPinAlarm);
    in.mode = GPIO_MODE_INPUT;
    in.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&in);
}

extern "C" void app_main() {
    initGpio();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_vfs_spiffs_conf_t fs = {"/spiffs", nullptr, 8, false};
    if (esp_vfs_spiffs_register(&fs) != ESP_OK) ESP_LOGW(TAG, "spiffs mount failed (run uploadfs)");

    startWifi();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ledInit(kPinLed);
    app.init();
    if (!startWebServer(app)) ESP_LOGE(TAG, "web server failed to start");

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        // Contacts pull to GND when closed.
        bool run = gpio_get_level(kPinRun) == 0;
        bool alarm = gpio_get_level(kPinAlarm) == 0;
        app.tick(run, alarm);

        app.lock();
        pump::Status st = app.controller().status();
        app.unlock();
        gpio_set_level(kPinAux, st.auxClosed ? 1 : 0);

        // Dim colours: red alarm, blue running, amber back-off, white disabled, green idle.
        if (st.alarm) ledSet(32, 0, 0);
        else if (st.running) ledSet(0, 0, 32);
        else if (st.state == pump::State::Backoff) ledSet(32, 16, 0);
        else if (st.state == pump::State::Disabled) ledSet(8, 8, 8);
        else ledSet(0, 16, 0);

        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}
