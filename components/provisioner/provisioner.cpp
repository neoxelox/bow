#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "cJSON.h"
#include "logger.hpp"
#include "status.hpp"
#include "database.hpp"
#include "provisioner.hpp"

namespace provisioner
{
    Credentials::Credentials(const char *SSID, const char *Password)
    {
        this->SSID = strdup(SSID);
        this->Password = strdup(Password);
    }

    Credentials::Credentials(cJSON *src)
    {
        this->SSID = strdup(cJSON_GetObjectItem(src, "ssid")->valuestring);
        this->Password = strdup(cJSON_GetObjectItem(src, "password")->valuestring);
    }

    Credentials::~Credentials()
    {
        free((void *)this->SSID);
        free((void *)this->Password);
    }

    void Credentials::JSON(cJSON **dst)
    {
        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "ssid", this->SSID);
        cJSON_AddStringToObject(root, "password", this->Password);

        *dst = root;
    }

    Provisioner *Provisioner::New(logger::Logger *logger, status::Controller *status, database::Database *database)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Provisioner();

        // Inject dependencies
        Instance->logger = logger;
        Instance->status = status;
        Instance->db = database->Open(DB_NAMESPACE);

        // Set provisioning status
        Instance->status->SetStatus(status::Statuses::Provisioning);

        // Initialize Wi-Fi NVS partition
        esp_err_t err = nvs_flash_init();
        // Wi-Fi NVS partition has been truncated or format cannot be recognized
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);

        // Initialize default event loop
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Register LwIP event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, ESP_EVENT_ANY_ID, Instance->ipFunc, NULL, NULL));

        // Initialize LwIP
        ESP_ERROR_CHECK(esp_netif_init());

        // Initialize Wi-Fi driver
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

        // Get stored Wi-Fi credentials
        cJSON *credsJSON = NULL;
        ESP_ERROR_CHECK(Instance->db->Get("credentials", &credsJSON));

        // Start in softAP mode
        if (credsJSON == NULL)
        {
            Instance->logger->Debug(TAG, "Wi-Fi credentials not found");
            Instance->logger->Debug(TAG, "Starting in softAP mode: {\"ssid\":\"%s\",\"password\":\"%s\"}", AP_SSID, AP_PASSWORD);
            Credentials creds = Credentials(AP_SSID, AP_PASSWORD);
            Instance->apStart(&creds);
        }
        // Start in station mode
        else
        {
            Instance->logger->Debug(TAG, "Wi-Fi credentials found");
            Instance->logger->Debug(TAG, "Starting in station mode: %s", cJSON_PrintUnformatted(credsJSON));
            Credentials creds = Credentials(credsJSON);
            Instance->staStart(&creds);
        }

        cJSON_Delete(credsJSON);

        return Instance;
    }

    void Provisioner::apStart(Credentials *creds)
    {
        if (this->apHandle != NULL)
            return;

        // Initialize Wi-Fi in AP mode
        this->apHandle = esp_netif_create_default_wifi_ap();

        // Register Wi-Fi AP event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, this->apFunc, NULL, &this->apEHInstance));

        // Configure Wi-Fi in AP mode
        wifi_config_t cfg = {
            .ap = {
                .channel = NULL,                // Use default channel
                .authmode = WIFI_AUTH_WPA2_PSK, // Set WPA2 as the weakest authmode to accept
                .ssid_hidden = false,           // Broadcast SSID
                .max_connection = AP_MAX_CLIENTS,
            },
        };
        strcpy((char *)cfg.ap.ssid, creds->SSID);
        strcpy((char *)cfg.ap.password, creds->Password);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_AP, 300));
        ESP_ERROR_CHECK(esp_wifi_set_country_code("ES", true));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // Disable Wi-Fi powersaving
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));

        // TODO: Set static IP or Captive Portal

        // Start Wi-Fi in AP mode
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    void Provisioner::apStop()
    {
        if (this->apHandle == NULL)
            return;

        // Stop Wi-Fi in AP mode
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

        // Unregister Wi-Fi AP event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, this->apEHInstance));
    }

    void Provisioner::staStart(Credentials *creds)
    {
        if (this->staHandle != NULL)
            return;

        // Initialize Wi-Fi in station mode
        this->staHandle = esp_netif_create_default_wifi_sta();

        // Register Wi-Fi station event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, this->staFunc, NULL, &this->staEHInstance));

        // Configure Wi-Fi in station mode
        wifi_config_t cfg = {
            .sta = {
                .scan_method = WIFI_FAST_SCAN, // Scan until the first matched AP is found
                .channel = NULL,               // Scan on all channels
                .threshold = {
                    .rssi = NULL,                   // Use the default minimum rssi to accept
                    .authmode = WIFI_AUTH_WPA2_PSK, // Set WPA2 as the weakest authmode to accept
                },
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH, // Enable Protected Management Frame for WPA3 networks
            },
        };
        strcpy((char *)cfg.sta.ssid, creds->SSID);
        strcpy((char *)cfg.sta.password, creds->Password);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_country_code("ES", true));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // Disable Wi-Fi powersaving
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

        // Start Wi-Fi in station mode
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    void Provisioner::staStop()
    {
        if (this->staHandle == NULL)
            return;

        // Disconnect from the target Wi-Fi network
        ESP_ERROR_CHECK(esp_wifi_disconnect());

        // Stop Wi-Fi in station mode
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

        // Unregister Wi-Fi station event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, this->staEHInstance));
    }

    void Provisioner::apFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // TODO: Make this better, else ifs?
        esp_netif_ip_info_t ipInfo;
        char ip[16 + 1];
        char netmask[16 + 1];
        char gateway[16 + 1];

        switch (id)
        {
        case WIFI_EVENT_AP_START:
            Instance->logger->Debug(TAG, "Started Wi-Fi as AP");

            ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ipInfo));

            sprintf(ip, IPSTR, IP2STR(&ipInfo.ip));
            sprintf(netmask, IPSTR, IP2STR(&ipInfo.netmask));
            sprintf(gateway, IPSTR, IP2STR(&ipInfo.gw));

            Instance->logger->Debug(TAG, "Got IP address %s | netmask %s | gateway %s", ip, netmask, gateway);
            Instance->status->SetStatus(status::Statuses::Idle);
            break;

        case WIFI_EVENT_AP_STOP:
            Instance->logger->Debug(TAG, "Stopped Wi-Fi as AP");
            Instance->status->SetStatus(status::Statuses::Error);
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            Instance->logger->Debug(TAG, "Client connected to the Wi-Fi network");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            Instance->logger->Debug(TAG, "Client disconnected from the Wi-Fi network");
            break;

        default:
            Instance->logger->Debug(TAG, "Unhandled Wi-Fi event %d", id);
            break;
        }
    }

    void Provisioner::staFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        switch (id)
        {
        case WIFI_EVENT_STA_START:
            Instance->logger->Debug(TAG, "Started Wi-Fi as station");
            Instance->status->SetStatus(status::Statuses::Provisioning);

            // Connect to target Wi-Fi network // TODO: Make it exponentially retriable
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;

        case WIFI_EVENT_STA_STOP:
            Instance->logger->Debug(TAG, "Stopped Wi-Fi as station");
            Instance->status->SetStatus(status::Statuses::Error);
            break;

        case WIFI_EVENT_STA_CONNECTED:
            Instance->logger->Debug(TAG, "Connected to the target Wi-Fi network");
            Instance->status->SetStatus(status::Statuses::Provisioning);

            // TODO: Set static IP
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            Instance->logger->Debug(TAG, "Disconnected from the target Wi-Fi network");
            Instance->status->SetStatus(status::Statuses::Error);

            // Reconnect to target Wi-Fi network // TODO: Make it exponentially retriable
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;

        case WIFI_EVENT_STA_BSS_RSSI_LOW:
            Instance->logger->Debug(TAG, "Wi-Fi connection weak");
            Instance->status->SetStatus(status::Statuses::Error);
            break;

        default:
            Instance->logger->Debug(TAG, "Unhandled Wi-Fi event %d", id);
            break;
        }
    }

    void Provisioner::ipFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // TODO: Make this better, else ifs?
        ip_event_got_ip_t *event;

        char ip[16 + 1];
        char netmask[16 + 1];
        char gateway[16 + 1];

        switch (id)
        {
        case IP_EVENT_STA_GOT_IP:
            event = (ip_event_got_ip_t *)data;

            sprintf(ip, IPSTR, IP2STR(&event->ip_info.ip));
            sprintf(netmask, IPSTR, IP2STR(&event->ip_info.netmask));
            sprintf(gateway, IPSTR, IP2STR(&event->ip_info.gw));

            Instance->logger->Debug(TAG, "Got IP address %s | netmask %s | gateway %s", ip, netmask, gateway);
            Instance->status->SetStatus(status::Statuses::Idle);
            break;

        case IP_EVENT_STA_LOST_IP:
            Instance->logger->Debug(TAG, "Lost IP address");
            Instance->status->SetStatus(status::Statuses::Error);
            break;

        default:
            Instance->logger->Debug(TAG, "Unhandled IP event %d", id);
            break;
        }
    }

    wifi_mode_t Provisioner::GetMode() const
    {
        wifi_mode_t mode;

        ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

        return mode;
    }
}