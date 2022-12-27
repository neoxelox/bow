#pragma once

#include "esp_wifi.h"
#include "esp_event.h"
#include "cJSON.h"
#include "logger.hpp"
#include "status.hpp"
#include "database.hpp"

namespace provisioner
{
    static const char *TAG = "provisioner";

    static const char *DB_NAMESPACE = "system";
    static const char *AP_SSID = "Diana Dot";
    static const char *AP_PASSWORD = "Y6LBBSMA";
    static const uint8_t AP_MAX_CLIENTS = 5;

    class Credentials
    {
    public:
        const char *SSID;
        const char *Password;

    public:
        ~Credentials();
        Credentials(const char *SSID, const char *Password);
        Credentials(cJSON *src);
        void JSON(cJSON **dst);
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

    private:
        void apStart(Credentials *creds);
        void apStop();
        void staStart(Credentials *creds);
        void staStop();
        static void apFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void staFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);

    public:
        inline static Provisioner *Instance;
        static Provisioner *New(logger::Logger *logger, status::Controller *status, database::Database *database);

    public:
        wifi_mode_t GetMode() const;
    };
}