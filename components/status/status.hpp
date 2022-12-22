#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "logger.hpp"
#include "gpio.hpp"

namespace status
{
    static const char *TAG = "status";

    static const uint32_t MIN_TASK_WAIT = 125; // Milliseconds

    static const int QUEUE_SIZE = 25;

    class Color
    {
    public:
        uint8_t Red;
        uint8_t Green;
        uint8_t Blue;
    };

    namespace Colors
    {
        static const Color Primary = {8, 0, 255};
        static const Color Success = {0, 255, 0};
        static const Color Info = {255, 255, 255};
        static const Color Warning = {255, 255, 0};
        static const Color Danger = {255, 0, 0};
        static const Color None = {0, 0, 0};
    }

    class Status
    {
    public:
        class Color Color;
        uint8_t Brightness;
        uint32_t Duration; // Milliseconds
        bool Loop;
    };

    namespace Statuses
    {
        static const Status Idle = {Colors::Primary, 100, NULL, true};
        static const Status Transmitted = {Colors::Success, 100, 250, false};
        static const Status Received = {Colors::Info, 100, 250, false};
        static const Status Provisioning = {Colors::Warning, 100, 1000, true};
        static const Status Error = {Colors::Danger, 100, 500, true};
        static const Status Off = {Colors::None, 0, NULL, true};
    }

    class Controller
    {
    private:
        logger::Logger *logger;
        gpio::Analog *redPin;
        gpio::Analog *greenPin;
        gpio::Analog *bluePin;
        TaskHandle_t taskHandle;
        QueueHandle_t queue;

    private:
        void setColor(Color color, uint8_t brightness);
        static void taskFunc(void *args);

    public:
        inline static Controller *Instance;
        static Controller *New(logger::Logger *logger);

    public:
        esp_err_t SetStatus(Status status);
    };
}