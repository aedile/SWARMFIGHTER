/*
 * input.cpp - medal controls for Galaga (portrait, like Pac-Man)
 *   tilt left/right (IMU roll) -> joystick
 *   BOOT button -> fire
 *   PWR short press -> coin, then start half a second later; long press (1 s) -> power off
 */
#include "input.h"
#include "qmi8658.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INPUT";
#define PIN_BTN_BOOT GPIO_NUM_9
#define PIN_BTN_PWR  GPIO_NUM_18
#define PIN_BAT_EN   GPIO_NUM_15
#define TILT_ON  20     /* roll units from qmi8658_get_tilt: ~1/128 g */
#define TILT_OFF 12
#define IMU_PERIOD_US 16000

static bool imu_ok, left_active, right_active, pwr_was_down;
static int64_t pwr_down_since, imu_last_us, coin_seq_start;
static int coin_seq;    /* 0 idle, 1 coin held, 2 gap, 3 start held */

void input_init(void)
{
    gpio_config_t bat = {}; bat.pin_bit_mask = 1ULL << PIN_BAT_EN; bat.mode = GPIO_MODE_OUTPUT; gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t io = {}; io.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR); io.mode = GPIO_MODE_INPUT; io.pull_up_en = GPIO_PULLUP_ENABLE; gpio_config(&io);
    i2c_config_t i2c = {}; i2c.mode = I2C_MODE_MASTER; i2c.sda_io_num = GPIO_NUM_8; i2c.scl_io_num = GPIO_NUM_7;
    i2c.sda_pullup_en = GPIO_PULLUP_ENABLE; i2c.scl_pullup_en = GPIO_PULLUP_ENABLE; i2c.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &i2c);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
    imu_ok = qmi8658_init();
    if (imu_ok) qmi8658_calibrate();
    ESP_LOGI(TAG, "input ready (IMU %s)", imu_ok ? "ok" : "missing");
}

void input_update(ga_input_t *in)
{
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool pwr = gpio_get_level(PIN_BTN_PWR) == 0;
    in->fire = boot;

    if (pwr && !pwr_was_down) pwr_down_since = now;
    if (pwr && now - pwr_down_since >= 1000000) {
        ESP_LOGI(TAG, "power off");
        gpio_set_level(PIN_BAT_EN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!pwr && pwr_was_down && now - pwr_down_since < 400000 && coin_seq == 0) { coin_seq = 1; coin_seq_start = now; }
    pwr_was_down = pwr;

    /* coin/start sequence: coin 100 ms, gap 400 ms, start 100 ms */
    int64_t el = now - coin_seq_start;
    in->coin1 = 0; in->start1 = 0;
    switch (coin_seq) {
        case 1: in->coin1 = 1; if (el > 100000) coin_seq = 2; break;
        case 2: if (el > 500000) coin_seq = 3; break;
        case 3: in->start1 = 1; if (el > 600000) coin_seq = 0; break;
        default: break;
    }

    if (imu_ok && now - imu_last_us >= IMU_PERIOD_US) {
        imu_last_us = now;
        int8_t pitch, roll;
        qmi8658_get_tilt(&pitch, &roll);
        int lr = -roll;                       /* same convention as PELLETINO */
        if (left_active) { if (lr > -TILT_OFF) left_active = false; }
        else if (lr <= -TILT_ON) { left_active = true; right_active = false; }
        if (right_active) { if (lr < TILT_OFF) right_active = false; }
        else if (lr >= TILT_ON) { right_active = true; left_active = false; }
        in->left = left_active;
        in->right = right_active;
    }
}
