#pragma once

#include "i2cdev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "logger.hpp"
#include "provisioner.hpp"

namespace chron
{
    static const char *TAG = "chron";

    static const char *TIME_ZONE = "UTC-1"; // POSIX functions require a negative offset
    static const char *NTP_SERVER_ADDRESS = "pool.ntp.org";
    static const TickType_t SYNC_PERIOD_RTC = (5 * 60 * 1000) / portTICK_PERIOD_MS; // 5 minutes
    static const uint32_t SYNC_PERIOD_NTP = 1 * 3600 * 1000;                        // 1 hour

    class Controller
    {
    private:
        logger::Logger *logger;
        provisioner::Provisioner *provisioner;
        i2c_dev_t dev;
        TaskHandle_t taskHandle;

    private:
        void tm_to_tv(struct tm *src, struct timeval *dst);
        void tv_to_tm(struct timeval *src, struct tm *dst);
        void start();
        void stop();
        static void taskFunc(void *args);
        static void wifiFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ntpFunc(struct timeval *now_tv);

    public:
        inline static Controller *Instance;
        static Controller *New(logger::Logger *logger, provisioner::Provisioner *provisioner);

    public:
        void Now(struct tm *dst);
    };
}