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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.hpp"
#include "status.hpp"
#include "database.hpp"
#include "capdns.h"
#include "provisioner.hpp"

namespace provisioner
{
    Credentials::Credentials()
    {
        this->SSID = NULL;
        this->Password = NULL;
        this->IP.Address = NULL;
        this->IP.Netmask = NULL;
        this->IP.Gateway = NULL;
    }

    Credentials::Credentials(const char *ssid, const char *password, struct IP ip)
    {
        this->SSID = strdup(ssid);
        this->Password = strdup(password);
        this->IP.Address = strdup(ip.Address);
        this->IP.Netmask = strdup(ip.Netmask);
        this->IP.Gateway = strdup(ip.Gateway);
    }

    Credentials::Credentials(cJSON *src)
    {
        this->SSID = strdup(cJSON_GetObjectItem(src, "ssid")->valuestring);
        this->Password = strdup(cJSON_GetObjectItem(src, "password")->valuestring);

        cJSON *ip = cJSON_GetObjectItem(src, "ip");
        this->IP.Address = strdup(cJSON_GetObjectItem(ip, "address")->valuestring);
        this->IP.Netmask = strdup(cJSON_GetObjectItem(ip, "netmask")->valuestring);
        this->IP.Gateway = strdup(cJSON_GetObjectItem(ip, "gateway")->valuestring);
    }

    Credentials::~Credentials()
    {
        free((void *)this->SSID);
        free((void *)this->Password);
        free((void *)this->IP.Address);
        free((void *)this->IP.Netmask);
        free((void *)this->IP.Gateway);
    }

    Credentials &Credentials::operator=(const Credentials &other)
    {
        if (this == &other)
            return *this;

        free((void *)this->SSID);
        free((void *)this->Password);
        free((void *)this->IP.Address);
        free((void *)this->IP.Netmask);
        free((void *)this->IP.Gateway);

        this->SSID = strdup(other.SSID);
        this->Password = strdup(other.Password);
        this->IP.Address = strdup(other.IP.Address);
        this->IP.Netmask = strdup(other.IP.Netmask);
        this->IP.Gateway = strdup(other.IP.Gateway);

        return *this;
    }

    cJSON *Credentials::JSON()
    {
        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "ssid", this->SSID);
        cJSON_AddStringToObject(root, "password", this->Password);

        cJSON *ip = cJSON_AddObjectToObject(root, "ip");
        cJSON_AddStringToObject(ip, "address", this->IP.Address);
        cJSON_AddStringToObject(ip, "netmask", this->IP.Netmask);
        cJSON_AddStringToObject(ip, "gateway", this->IP.Gateway);

        return root;
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

        // Initialize LwIP
        ESP_ERROR_CHECK(esp_netif_init());

        // Initialize Wi-Fi driver
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

        // Initialize Wi-Fi handlers in AP mode
        Instance->apHandle = esp_netif_create_default_wifi_ap();

        // Initialize Wi-Fi handlers in station mode
        Instance->staHandle = esp_netif_create_default_wifi_sta();

        // Create provisioner delayed startup task and Wi-Fi station retrier
        xTaskCreatePinnedToCore(Instance->taskFunc, "Provisioner", 4 * 1024, NULL, 11, &Instance->taskHandle, tskNO_AFFINITY);

        return Instance;
    }

    void Provisioner::apStart(Credentials *creds)
    {
        if (this->GetMode() == WIFI_MODE_AP)
            return;

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
        ESP_ERROR_CHECK(esp_wifi_set_country_code("ES", true));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // Disable Wi-Fi powersaving
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));

        // Set static IP
        esp_netif_ip_info_t ipInfo;
        ipInfo.ip.addr = ipaddr_addr(creds->IP.Address);
        ipInfo.netmask.addr = ipaddr_addr(creds->IP.Netmask);
        ipInfo.gw.addr = ipaddr_addr(creds->IP.Gateway);

        ESP_ERROR_CHECK(esp_netif_dhcps_stop(this->apHandle));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(this->apHandle, &ipInfo));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(this->apHandle));

        // Start Wi-Fi in AP mode
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    void Provisioner::apStop()
    {
        if (this->GetMode() != WIFI_MODE_AP)
            return;

        // Stop Wi-Fi in AP mode
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    }

    void Provisioner::staStart(Credentials *creds)
    {
        if (this->GetMode() == WIFI_MODE_STA)
            return;

        // Reset retries
        this->staRetries = 0;

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
        if (this->GetMode() != WIFI_MODE_STA)
            return;

        // Signal that the disconnection is handled and retrying is not wanted
        this->staRetries = -1;

        // Disconnect from the target Wi-Fi network
        ESP_ERROR_CHECK(esp_wifi_disconnect());

        // Stop Wi-Fi in station mode
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    }

    void Provisioner::apFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        switch (id)
        {
        case WIFI_EVENT_AP_START:
        {
            Instance->logger->Debug(TAG, "Started Wi-Fi as access point");

            // Start captive portal DNS server
            capdns_start(AP_CAPTIVE_PORTAL_PRIORITY);

            esp_netif_ip_info_t ipInfo;
            ESP_ERROR_CHECK(esp_netif_get_ip_info(Instance->apHandle, &ipInfo));

            char ip[16 + 1], netmask[16 + 1], gateway[16 + 1];
            sprintf(ip, IPSTR, IP2STR(&ipInfo.ip));
            sprintf(netmask, IPSTR, IP2STR(&ipInfo.netmask));
            sprintf(gateway, IPSTR, IP2STR(&ipInfo.gw));

            Instance->logger->Debug(TAG, "Got IP: Address=%s | Netmask=%s | Gateway=%s", ip, netmask, gateway);
            Instance->status->SetStatus(status::Statuses::Idle);
            break;
        }

        case WIFI_EVENT_AP_STOP:
        {
            // Stop captive portal DNS server
            capdns_stop();

            Instance->logger->Debug(TAG, "Stopped Wi-Fi as access point");
            Instance->status->SetStatus(status::Statuses::Error);
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED:
        {
            Instance->logger->Debug(TAG, "Client connected to the Wi-Fi network");
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            Instance->logger->Debug(TAG, "Client disconnected from the Wi-Fi network");
            break;
        }
        }
    }

    void Provisioner::staFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        switch (id)
        {
        case WIFI_EVENT_STA_START:
        {
            Instance->logger->Debug(TAG, "Started Wi-Fi as station");
            Instance->status->SetStatus(status::Statuses::Provisioning);

            // Connect to target Wi-Fi network
            Instance->logger->Debug(TAG, "Trying to connect to the target Wi-Fi network");
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        }

        case WIFI_EVENT_STA_STOP:
        {
            Instance->logger->Debug(TAG, "Stopped Wi-Fi as station");
            Instance->status->SetStatus(status::Statuses::Error);
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
        {
            Instance->logger->Debug(TAG, "Connected to the target Wi-Fi network");
            Instance->status->SetStatus(status::Statuses::Provisioning);

            // Reset retries
            Instance->staRetries = 0;

            // Stop DHCP client to set static IP
            ESP_ERROR_CHECK(esp_netif_dhcpc_stop(Instance->staHandle));

            // Get stored Wi-Fi credentials
            Credentials *creds = Instance->GetCreds();
            if (creds == NULL)
                ESP_ERROR_CHECK(esp_wifi_disconnect());

            // Set static IP
            esp_netif_ip_info_t ipInfo;
            ipInfo.ip.addr = ipaddr_addr(creds->IP.Address);
            ipInfo.netmask.addr = ipaddr_addr(creds->IP.Netmask);
            ipInfo.gw.addr = ipaddr_addr(creds->IP.Gateway);

            delete creds;

            esp_err_t err = esp_netif_set_ip_info(Instance->staHandle, &ipInfo);

            // Restart DHCP client and force Wi-Fi disconnection to enter
            // Wi-Fi station retry loop if static IP cannot be set
            if (err != ESP_OK)
            {
                ESP_ERROR_CHECK(esp_netif_dhcpc_start(Instance->staHandle));
                ESP_ERROR_CHECK(esp_wifi_disconnect());
                break;
            }

            // Set DNS servers to be able to resolve addresses with a static IP
            esp_netif_dns_info_t dnsMainInfo, dnsBackupInfo;
            dnsMainInfo.ip.u_addr.ip4.addr = ipaddr_addr(STA_DNS_MAIN_SERVER_ADDRESS);
            dnsMainInfo.ip.type = IPADDR_TYPE_V4;
            ESP_ERROR_CHECK(esp_netif_set_dns_info(Instance->staHandle, ESP_NETIF_DNS_MAIN, &dnsMainInfo));
            dnsBackupInfo.ip.u_addr.ip4.addr = ipaddr_addr(STA_DNS_BACKUP_SERVER_ADDRESS);
            dnsBackupInfo.ip.type = IPADDR_TYPE_V4;
            ESP_ERROR_CHECK(esp_netif_set_dns_info(Instance->staHandle, ESP_NETIF_DNS_BACKUP, &dnsBackupInfo));

            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            Instance->logger->Debug(TAG, "Disconnected from the target Wi-Fi network");
            Instance->status->SetStatus(status::Statuses::Error);

            // If retried for too long switch to softAP mode fallback
            if (Instance->staRetries == STA_MAX_RETRIES)
            {
                Instance->logger->Debug(TAG, "Fallbacking into softAP mode: SSID=%s | Password=%s", AP_SSID, AP_PASSWORD);
                Instance->staStop();
                Credentials creds = Credentials(AP_SSID, AP_PASSWORD, {AP_STATIC_IP_ADDRESS, AP_STATIC_IP_NETMASK, AP_STATIC_IP_GATEWAY});
                Instance->apStart(&creds);
            }
            // Ignore further Wi-Fi station disconnections. This can happen because when station
            // is stopped, it disconnects from the target Wi-Fi and triggers an additional event
            else if (Instance->staRetries > STA_MAX_RETRIES)
            {
                break;
            }
            // If disconnection is handled retrying is not wanted
            else if (Instance->staRetries == -1)
            {
                break;
            }
            // Otherwise try to reconnect to target Wi-Fi network
            else
            {
                Instance->staRetries++;
                Instance->logger->Debug(TAG, "Retrying to connect to the target Wi-Fi network: %d/%d", Instance->staRetries, STA_MAX_RETRIES);
                ESP_ERROR_CHECK(esp_wifi_connect());
            }

            break;
        }

        case WIFI_EVENT_STA_BSS_RSSI_LOW:
        {
            Instance->logger->Debug(TAG, "Target Wi-Fi network connection weak");
            Instance->status->SetStatus(status::Statuses::Error);
            break;
        }
        }
    }

    void Provisioner::ipFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        switch (id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;

            char ip[16 + 1], netmask[16 + 1], gateway[16 + 1];
            sprintf(ip, IPSTR, IP2STR(&event->ip_info.ip));
            sprintf(netmask, IPSTR, IP2STR(&event->ip_info.netmask));
            sprintf(gateway, IPSTR, IP2STR(&event->ip_info.gw));

            Instance->logger->Debug(TAG, "Got IP: Address=%s | Netmask=%s | Gateway=%s", ip, netmask, gateway);

            esp_netif_dns_info_t dnsMainInfo, dnsBackupInfo;
            ESP_ERROR_CHECK(esp_netif_get_dns_info(Instance->staHandle, ESP_NETIF_DNS_MAIN, &dnsMainInfo));
            ESP_ERROR_CHECK(esp_netif_get_dns_info(Instance->staHandle, ESP_NETIF_DNS_BACKUP, &dnsBackupInfo));

            char dnsMain[16 + 1], dnsBackup[16 + 1];
            sprintf(dnsMain, IPSTR, IP2STR(&dnsMainInfo.ip.u_addr.ip4));
            sprintf(dnsBackup, IPSTR, IP2STR(&dnsBackupInfo.ip.u_addr.ip4));

            Instance->logger->Debug(TAG, "Got DNS: Main=%s | Backup=%s", dnsMain, dnsBackup);

            Instance->status->SetStatus(status::Statuses::Idle);
            break;
        }
        }
    }

    void Provisioner::taskFunc(void *args)
    {
        Credentials *creds = NULL;

        // Delay provisioner startup to let the other components initialize first
        vTaskDelay(STARTUP_DELAY);

        // Register Wi-Fi AP, station and LwIP event callbacks for
        // provisioner after all components have been initialized
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, Instance->apFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, Instance->staFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, ESP_EVENT_ANY_ID, Instance->ipFunc, NULL, NULL));

        // Get stored Wi-Fi credentials
        creds = Instance->GetCreds();

        // Start in station mode if credentials exist
        if (creds != NULL)
        {
            Instance->logger->Debug(TAG, "Wi-Fi credentials found");
            Instance->logger->Debug(TAG, "Starting in station mode: SSID=%s | Password=%s", creds->SSID, creds->Password);
            Instance->staStart(creds);
        }
        // Start in softAP mode if credentials don't exist
        else
        {
            Instance->logger->Debug(TAG, "Wi-Fi credentials not found");
            Instance->logger->Debug(TAG, "Starting in softAP mode: SSID=%s | Password=%s", AP_SSID, AP_PASSWORD);
            creds = new Credentials(AP_SSID, AP_PASSWORD, {AP_STATIC_IP_ADDRESS, AP_STATIC_IP_NETMASK, AP_STATIC_IP_GATEWAY});
            Instance->apStart(creds);
        }

        delete creds;

        while (1)
        {
            vTaskDelay(STA_RETRY_PERIOD);

            // Get current Wi-Fi mode
            wifi_mode_t mode = Instance->GetMode();

            // Get stored Wi-Fi credentials
            creds = Instance->GetCreds();

            // Get current Wi-Fi network
            wifi_ap_record_t current;
            if (mode == WIFI_MODE_STA)
                Instance->GetCurrent(&current);

            // Go into station mode if credentials exist and Wi-Fi is not already station
            // or the network is different from the current one
            if (creds != NULL && (mode != WIFI_MODE_STA || strcmp(creds->SSID, (char *)current.ssid)))
            {
                Instance->logger->Debug(TAG, "Retrying into station mode: SSID=%s | Password=%s", creds->SSID, creds->Password);
                if (mode == WIFI_MODE_STA)
                    Instance->staStop();
                else
                    Instance->apStop();
                Instance->staStart(creds);
            }

            delete creds;
        }
    }

    wifi_mode_t Provisioner::GetMode() const
    {
        wifi_mode_t mode;

        ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

        return mode;
    }

    void Provisioner::GetCurrent(wifi_ap_record_t *current)
    {
        ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(current));
    }

    void Provisioner::GetAvailable(wifi_ap_record_t *available, uint32_t *size)
    {
        // TODO: Make this work on softAP mode
        if (this->GetMode() != WIFI_MODE_STA)
        {
            *size = 0;
            return;
        }

        *size = SCAN_MAX_NETWORKS;
        ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records((uint16_t *)size, available));
        ESP_ERROR_CHECK(esp_wifi_scan_stop());
    }

    void Provisioner::Retry()
    {
        // Suspend is needed because the provisioner task is in blocked state by delay
        vTaskSuspend(this->taskHandle);
        vTaskResume(this->taskHandle);
    }

    Credentials *Provisioner::GetCreds()
    {
        Credentials *creds = NULL;

        cJSON *credsJSON = NULL;
        ESP_ERROR_CHECK(this->db->Get("credentials", &credsJSON));

        if (credsJSON != NULL)
            creds = new Credentials(credsJSON);

        cJSON_Delete(credsJSON);

        return creds;
    }

    void Provisioner::SetCreds(Credentials *creds)
    {
        cJSON *credsJSON = creds->JSON();
        ESP_ERROR_CHECK(this->db->Set("credentials", credsJSON));
        cJSON_Delete(credsJSON);
    }
}