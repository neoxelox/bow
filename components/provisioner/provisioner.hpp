#pragma once

#include "esp_event.h"
#include "logger.hpp"
#include "status.hpp"

namespace provisioner
{
    static const char *TAG = "provisioner";

    class Provisioner
    {
    private:
        logger::Logger *logger;
        status::Controller *status;

    private:
        static void wifiFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);

    public:
        inline static Provisioner *Instance;
        static Provisioner *New(logger::Logger *logger, status::Controller *status);
    };
}