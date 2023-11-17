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
    static const TickType_t STARTUP_DELAY = (5 * 1000) / portTICK_PERIOD_MS; // 5 seconds

    static const char *AP_SSID = "Diana Dot";
    static const char *AP_PASSWORD = "Y6LBBSMA";
    static const char *AP_STATIC_IP_ADDRESS = "192.168.1.1";
    static const char *AP_STATIC_IP_NETMASK = "255.255.255.0";
    static const char *AP_STATIC_IP_GATEWAY = "192.168.1.1";
    static const uint8_t AP_MAX_CLIENTS = 5;
    static const uint8_t AP_CAPTIVE_PORTAL_PRIORITY = 16;

    static const int32_t STA_MAX_RETRIES = 10;
    static const TickType_t STA_RETRY_PERIOD = (5 * 60 * 1000) / portTICK_PERIOD_MS; // 5 minutes
    static const char *STA_DNS_MAIN_SERVER_ADDRESS = "1.1.1.1";
    static const char *STA_DNS_BACKUP_SERVER_ADDRESS = "8.8.8.8";

    typedef struct IP
    {
        const char *Address;
        const char *Netmask;
        const char *Gateway;
    } IP;

    class Credentials
    {
    public:
        const char *SSID;
        const char *Password;
        struct IP IP;

    public:
        Credentials();
        Credentials(const char *ssid, const char *password, struct IP ip);
        Credentials(cJSON *src);
        ~Credentials();
        Credentials &operator=(const Credentials &other);
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
        int32_t staRetries;
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
        void GetCurrent(wifi_ap_record_t *current);
        void Retry();
        Credentials *GetCreds();
        void SetCreds(Credentials *creds);
    };
}