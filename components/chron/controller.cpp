#include "esp_err.h"
#include "driver/gpio.h"
#include "i2cdev.h"
#include "ds3231.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "logger.hpp"
#include "provisioner.hpp"
#include "chron.hpp"

namespace chron
{
    Controller *Controller::New(logger::Logger *logger, provisioner::Provisioner *provisioner)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Controller();

        // Inject dependencies
        Instance->logger = logger;
        Instance->provisioner = provisioner;

        // Initialize DS3231 RTC
        ESP_ERROR_CHECK(i2cdev_init());
        ESP_ERROR_CHECK(ds3231_init_desc(&Instance->dev, I2C_NUM_0, GPIO_NUM_43, GPIO_NUM_44));

        // Configure DS3231 RTC
        ESP_ERROR_CHECK(ds3231_clear_oscillator_stop_flag(&Instance->dev));
        ESP_ERROR_CHECK(ds3231_clear_alarm_flags(&Instance->dev, DS3231_ALARM_BOTH));
        ESP_ERROR_CHECK(ds3231_disable_alarm_ints(&Instance->dev, DS3231_ALARM_BOTH));
        ESP_ERROR_CHECK(ds3231_disable_32khz(&Instance->dev));
        ESP_ERROR_CHECK(ds3231_disable_squarewave(&Instance->dev));

        // Set time zone
        setenv("TZ", TIME_ZONE, 1);
        tzset();

        // Register Wi-Fi station and LwIP event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, Instance->staFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, Instance->ipFunc, NULL, NULL));

        // Create RTC sync task
        xTaskCreatePinnedToCore(Instance->taskFunc, "Chron", 4 * 1024, NULL, 8, &Instance->taskHandle, tskNO_AFFINITY);

        return Instance;
    }

    void Controller::tm_to_tv(struct tm *src, struct timeval *dst)
    {
        dst->tv_sec = mktime(src);
        dst->tv_usec = 0;
    }

    void Controller::tv_to_tm(struct timeval *src, struct tm *dst)
    {
        time_t aux = src->tv_sec;
        localtime_r(&aux, dst);
    }

    void Controller::Now(struct tm *dst)
    {
        struct timeval src;
        gettimeofday(&src, NULL);
        this->tv_to_tm(&src, dst);
    }

    void Controller::start()
    {
        if (sntp_enabled())
            return;

        // Configure NTP sync
        sntp_set_time_sync_notification_cb(this->ntpFunc);
        sntp_set_sync_interval(SYNC_PERIOD_NTP);
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, NTP_SERVER_ADDRESS);

        // Start NTP sync
        sntp_init();

        this->logger->Debug(TAG, "Started NTP sync with server %s", NTP_SERVER_ADDRESS);
    }

    void Controller::stop()
    {
        if (!sntp_enabled())
            return;

        // Stop NTP sync
        sntp_stop();

        this->logger->Debug(TAG, "Stopped NTP sync");
    }

    void Controller::staFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Stop NTP sync when Wi-Fi station is disconnected because underlying sockets are freed
        // WIFI_EVENT_STA_DISCONNECTED
        Instance->stop();
    }

    void Controller::ipFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Start NTP sync when IP is got
        // IP_EVENT_STA_GOT_IP
        Instance->start();
    }

    void Controller::taskFunc(void *args)
    {
        struct tm now_tm;
        struct timeval now_tv;
        char now_str[64];

        while (1)
        {
            // Sync system with RTC
            ESP_ERROR_CHECK(ds3231_get_time(&Instance->dev, &now_tm));
            Instance->tm_to_tv(&now_tm, &now_tv);
            settimeofday(&now_tv, NULL);

            strftime(now_str, sizeof(now_str), "%c %Z", &now_tm);
            Instance->logger->Debug(TAG, "Synced with RTC at %s", now_str);
            vTaskDelay(SYNC_PERIOD_RTC);
        }
    }

    void Controller::ntpFunc(struct timeval *now_tv)
    {
        struct tm now_tm;
        char now_str[64];

        // Sync RTC with NTP
        Instance->tv_to_tm(now_tv, &now_tm);
        ESP_ERROR_CHECK(ds3231_set_time(&Instance->dev, &now_tm));

        strftime(now_str, sizeof(now_str), "%c %Z", &now_tm);
        Instance->logger->Debug(TAG, "Synced with NTP at %s", now_str);
    }
}