#include <cstring>

#include "app.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "nvs_flash.h"
#include "web.h"
#include "wifi.h"

// Free on ESP32-S3 N16R8 (avoids octal PSRAM 35-37, strapping and USB pins).
constexpr gpio_num_t kPinRun = GPIO_NUM_4;    // drive Run relay contact to GND
constexpr gpio_num_t kPinAlarm = GPIO_NUM_5;  // drive Alarm relay contact to GND
constexpr gpio_num_t kPinAux = GPIO_NUM_6;    // relay module: high energizes the coil, opening the NC contact (pump stopped)
constexpr gpio_num_t kPinBoot = GPIO_NUM_0;   // BOOT button: hold to re-enter the Wi-Fi network
constexpr int kPinLed = 48;                   // on-board WS2812
constexpr int kBootHoldTicks = 20;            // 2 s at the 100 ms loop rate

static const char* TAG = "main";
static App app;
static bool g_webOk = false;

// A freshly updated image must prove itself or the bootloader reverts to the previous one.
static void checkOtaHealth() {
    constexpr int64_t kConfirmUs = 30LL * 1000 * 1000;
    constexpr int64_t kGiveUpUs = 5LL * 60 * 1000 * 1000;
    static bool done = false;
    if (done) return;

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        done = true;
        return;
    }
    const int64_t up = esp_timer_get_time();
    // With no saved network the image is fine but waiting for setup, so only a saved one must get online.
    if ((wifi::connected() || !wifi::hasCredentials()) && g_webOk && up > kConfirmUs) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed");
        done = true;
    } else if (up > kGiveUpUs) {
        ESP_LOGE(TAG, "new firmware never came up, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

static void initGpio() {
    // Energize the relay (pump stopped) until the controller decides.
    gpio_config_t out = {};
    out.pin_bit_mask = 1ULL << kPinAux;
    out.mode = GPIO_MODE_OUTPUT;
    gpio_set_level(kPinAux, 1);
    gpio_config(&out);
    gpio_set_level(kPinAux, 1);

    gpio_config_t in = {};
    in.pin_bit_mask = (1ULL << kPinRun) | (1ULL << kPinAlarm) | (1ULL << kPinBoot);
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

    wifi::init();
    // Credentials already saved: join without asking. Otherwise ask over the serial port.
    if (wifi::hasCredentials()) wifi::connectStored();
    else wifi::startProvisioning();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ledInit(kPinLed);
    app.init();
    g_webOk = startWebServer(app);
    if (!g_webOk) ESP_LOGE(TAG, "web server failed to start");

    TickType_t last = xTaskGetTickCount();
    int bootHeld = 0;
    for (;;) {
        // Setup runs in its own task, so the pump keeps being controlled while it waits for typing.
        if (gpio_get_level(kPinBoot) == 0) {
            if (++bootHeld == kBootHoldTicks) wifi::startProvisioning();
        } else {
            bootHeld = 0;
        }

        // Contacts pull to GND when closed.
        bool run = gpio_get_level(kPinRun) == 0;
        bool alarm = gpio_get_level(kPinAlarm) == 0;
        app.tick(run, alarm);

        app.lock();
        pump::Status st = app.controller().status();
        app.unlock();
        gpio_set_level(kPinAux, st.auxClosed ? 0 : 1);

        // Dim colours: red alarm, purple Wi-Fi setup, blue running, amber back-off, white disabled, green idle.
        if (st.alarm) ledSet(32, 0, 0);
        else if (wifi::provisioning()) ledSet(24, 0, 24);
        else if (st.running) ledSet(0, 0, 32);
        else if (st.state == pump::State::Backoff) ledSet(32, 16, 0);
        else if (st.state == pump::State::Disabled) ledSet(8, 8, 8);
        else ledSet(0, 16, 0);

        checkOtaHealth();

        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}
