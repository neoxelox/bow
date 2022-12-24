#include <errno.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "http_parser.h"
#include "cJSON.h"
#include "logger.hpp"
#include "provisioner.hpp"
#include "server.hpp"

namespace server
{
    Server *Server::New(logger::Logger *logger, provisioner::Provisioner *provisioner)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Server();

        // Inject dependencies
        Instance->logger = logger;
        Instance->provisioner = provisioner;

        Instance->espServer = NULL;

        // Register Wi-Fi and LwIP event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, Instance->wifiFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, Instance->ipFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_LOST_IP, Instance->ipFunc, NULL, NULL));

        return Instance;
    }

    void Server::start()
    {
        if (this->espServer != NULL)
            return;

        // Configure HTTP server
        httpd_config_t cfg = {
            .task_priority = 9,
            .stack_size = 4 * 1024,
            .core_id = tskNO_AFFINITY,
            .server_port = 80,
            .ctrl_port = 32768,
            .max_open_sockets = 5,
            .max_uri_handlers = 5,
            .max_resp_headers = 10,
            .backlog_conn = 5,
            .lru_purge_enable = true,
            .recv_wait_timeout = 5,
            .send_wait_timeout = 5,
            .uri_match_fn = httpd_uri_match_wildcard,
        };

        // Start HTTP server
        ESP_ERROR_CHECK(httpd_start(&this->espServer, &cfg));

        // Register error handlers, note that there is no way to register all at the same time
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_400_BAD_REQUEST, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_401_UNAUTHORIZED, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_403_FORBIDDEN, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_404_NOT_FOUND, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_405_METHOD_NOT_ALLOWED, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_408_REQ_TIMEOUT, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_411_LENGTH_REQUIRED, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_414_URI_TOO_LONG, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_500_INTERNAL_SERVER_ERROR, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_501_METHOD_NOT_IMPLEMENTED, this->errHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_505_VERSION_NOT_SUPPORTED, this->errHandler));

        // Register HTTP handlers // TODO: Move it somehow to the hpp?

        // TODO: And endpoint that gives you:
        // - Chip info
        // - Firmware info
        // - Tasks info
        // - Resource info
        // - Time info
        // - Storage info (FATFS/NVS)

        // TODO: logger middleware

        httpd_uri_t getURIHandler = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = this->getHandler,
        };

        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &getURIHandler));

        this->logger->Debug(TAG, "Started HTTP server on port :%d", cfg.server_port);
    }

    void Server::stop()
    {
        if (this->espServer == NULL)
            return;

        // Stop HTTP server
        ESP_ERROR_CHECK(httpd_stop(this->espServer));

        this->espServer = NULL;

        this->logger->Debug(TAG, "Stopped HTTP server");
    }

    void Server::wifiFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Stop HTTP server when Wi-Fi is disconnected because underlying sockets are freed
        // WIFI_EVENT_STA_DISCONNECTED
        Instance->stop();
    }

    void Server::ipFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Start HTTP server when IP is got
        if (id == IP_EVENT_STA_GOT_IP)
            Instance->start();

        // Stop HTTP server when IP is lost because underlying sockets are freed
        else // IP_EVENT_STA_LOST_IP
            Instance->stop();
    }

    esp_err_t Server::errHandler(httpd_req_t *request, httpd_err_code_t error)
    {
        const char *status;
        cJSON *root = cJSON_CreateObject();

        switch (error)
        {
        case HTTPD_400_BAD_REQUEST:
            status = HTTPD_400;
            cJSON_AddStringToObject(root, "code", "ERR_INVALID_REQUEST");
            break;

        case HTTPD_404_NOT_FOUND:
            status = HTTPD_404;
            cJSON_AddStringToObject(root, "code", "ERR_NOT_FOUND");
            break;

        default:
            status = HTTPD_500;
            cJSON_AddStringToObject(root, "code", "ERR_SERVER_GENERIC");
            break;
        }

        const char *body = cJSON_PrintUnformatted(root);

        // TODO: Set required headers with httpd_resp_set_hdr
        ESP_ERROR_CHECK(httpd_resp_set_status(request, status));
        ESP_ERROR_CHECK(httpd_resp_set_type(request, HTTPD_TYPE_JSON));

        // Framework-necessary code
#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
        int fd = httpd_req_to_sockfd(request);
        int nodelay = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
        {
            Instance->logger->Warn(TAG, "error calling setsockopt: %d", errno);
            nodelay = 0;
        }
#endif
        // ------------------------

        ESP_ERROR_CHECK(httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN));

        // Framework-necessary code
#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
        if (nodelay == 1)
        {
            nodelay = 0;
            if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
            {
                Instance->logger->Error(TAG, "error calling setsockopt: %d", errno);
                return ESP_ERR_INVALID_STATE;
            }
        }
#endif
        // ------------------------

        free((void *)body);
        cJSON_Delete(root);

        return ESP_FAIL;
    }

    esp_err_t Server::getHandler(httpd_req_t *request)
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "message", "Hello World!");
        const char *body = cJSON_PrintUnformatted(root);

        // TODO: Set required headers with httpd_resp_set_hdr
        // TODO: NOTE: FOR WEB DASHBOARD: Cache-Control: max-age=604800, stale-while-revalidate=86400, tale-if-error=86400
        ESP_ERROR_CHECK(httpd_resp_set_status(request, HTTPD_200));
        ESP_ERROR_CHECK(httpd_resp_set_type(request, HTTPD_TYPE_JSON));
        ESP_ERROR_CHECK(httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN));

        free((void *)body);
        cJSON_Delete(root);

        return ESP_OK;
    }
}