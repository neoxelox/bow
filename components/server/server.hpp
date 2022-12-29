#pragma once

#include "esp_event.h"
#include "esp_http_server.h"
#include "http_parser.h"
#include "esp_vfs_fat.h"
#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"
#include "provisioner.hpp"
#include "chron.hpp"
#include "device.hpp"
#include "user.hpp"

namespace server
{
    static const char *TAG = "server";

    static const char *PARTITION = "static";
    static const char *ROOT = "/static";
    static const char *FRONTEND = "/static/frontend.gz";

    static const uint16_t PORT = 80;
    static const uint16_t MAX_CLIENTS = 5;
    static const uint32_t MAX_REQUEST_CONTENT_SIZE = 1024;

    namespace Errors
    {
        static const char *InvalidRequest = "ERR_INVALID_REQUEST";
        static const char *NotFound = "ERR_NOT_FOUND";
        static const char *ServerGeneric = "ERR_SERVER_GENERIC";
    }

    namespace Methods
    {
        static const httpd_method_t GET = HTTP_GET;
        static const httpd_method_t POST = HTTP_POST;
        static const httpd_method_t PUT = HTTP_PUT;
        static const httpd_method_t DELETE = HTTP_DELETE;
    }

    namespace Statuses
    {
        static const char *_200 = "200 OK";
        static const char *_204 = "204 No Content";
        static const char *_301 = "301 Moved Permanently";
        static const char *_302 = "302 Temporary Redirect";
        static const char *_400 = "400 Bad Request";
        static const char *_404 = "404 Not Found";
        static const char *_500 = "500 Internal Server Error";
    }

    namespace ContentTypes
    {
        static const char *ApplicationJSON = "application/json";
        static const char *ApplicationOctetStream = "application/octet-stream";
        static const char *TextHTML = "text/html";
    }

    namespace ContentEncodings
    {
        static const char *GZIP = "gzip";
    }

    namespace Headers
    {
        static const char *Location = "Location";
        static const char *CacheControl = "Cache-Control";
        static const char *ContentEncoding = "Content-Encoding";
        static const char *Connection = "Connection";
    }

    class Server
    {
    private:
        logger::Logger *logger;
        database::Database *database;
        provisioner::Provisioner *provisioner;
        chron::Controller *chron;
        device::Transmitter *transmitter;
        device::Receiver *receiver;
        user::Controller *user;
        httpd_handle_t espServer;
        wl_handle_t fsHandle;
        httpd_uri_t frontURIHandler = {"/*", Methods::GET, frontHandler};
        httpd_uri_t apiPostRegisterURIHandler = {"/api/register", Methods::POST, apiPostRegisterHandler};
        httpd_uri_t apiPostLoginURIHandler = {"/api/login", Methods::POST, apiPostLoginHandler};

    private:
        void start();
        void stop();
        esp_err_t sendFile(httpd_req_t *request, const char *path, const char *status);
        esp_err_t sendJSON(httpd_req_t *request, cJSON *json, const char *status);
        esp_err_t sendError(httpd_req_t *request, const char *message);
        esp_err_t recvJSON(httpd_req_t *request, cJSON **json);
        static void apFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void staFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static esp_err_t errorHandler(httpd_req_t *request, httpd_err_code_t error);
        static esp_err_t frontHandler(httpd_req_t *request);
        static esp_err_t apiPostRegisterHandler(httpd_req_t *request);
        static esp_err_t apiPostLoginHandler(httpd_req_t *request);

    public:
        inline static Server *Instance;
        static Server *New(logger::Logger *logger, database::Database *database, provisioner::Provisioner *provisioner,
                           chron::Controller *chron, device::Transmitter *transmitter, device::Receiver *receiver,
                           user::Controller *user);
    };
}