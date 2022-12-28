#pragma once

#include "esp_wifi.h"
#include "esp_event.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.hpp"
#include "status.hpp"
#include "database.hpp"

namespace provisioner
{
    static const char *TAG = "provisioner";

    static const char *DB_NAMESPACE = "system";
    static const TickType_t STARTUP_DELAY = (5 * 1000) / portTICK_PERIOD_MS;     // 5 seconds
    static const TickType_t RETRY_PERIOD = (5 * 60 * 1000) / portTICK_PERIOD_MS; // 5 minutes

    static const char *AP_SSID = "Diana Dot";
    static const char *AP_PASSWORD = "Y6LBBSMA";
    static const uint8_t AP_MAX_CLIENTS = 5;

    static const uint8_t STA_MAX_RETRIES = 10;

    class Credentials
    {
    public:
        const char *SSID;
        const char *Password;

    public:
        ~Credentials();
        Credentials(const char *SSID, const char *Password);
        Credentials(cJSON *src);
        cJSON *JSON();
    };

    class Provisioner
    {
    private:
        logger::Logger *logger;
        status::Controller *status;
        database::Handle *db;
        esp_netif_t *apHandle;
        esp_netif_t *staHandle;
        esp_event_handler_instance_t apEHInstance;
        esp_event_handler_instance_t staEHInstance;
        uint8_t staRetries;
        TaskHandle_t taskHandle;

    private:
        void apStart(Credentials *creds);
        void apStop();
        void staStart(Credentials *creds);
        void staStop();
        static void taskFunc(void *args);
        static void apFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void staFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);

    public:
        inline static Provisioner *Instance;
        static Provisioner *New(logger::Logger *logger, status::Controller *status, database::Database *database);

    public:
        wifi_mode_t GetMode() const;
        Credentials *GetCreds();
        void SetCreds(Credentials *creds);
    };
}