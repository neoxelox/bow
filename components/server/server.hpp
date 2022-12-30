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
#include "user.hpp"
#include "device.hpp"
#include "trigger.hpp"
#include "role.hpp"

namespace server
{
    static const char *TAG = "server";

    static const char *PARTITION = "static";
    static const char *ROOT = "/static";
    static const char *FRONTEND = "/static/frontend.gz";

    static const uint16_t PORT = 80;
    static const uint16_t MAX_CLIENTS = 5;
    static const uint32_t MAX_REQUEST_HEADER_SIZE = 128;
    static const uint32_t MAX_REQUEST_CONTENT_SIZE = 1024;

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
        static const char *_401 = "401 Unauthorized";
        static const char *_403 = "403 Forbidden";
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
        static const char *Authorization = "Authorization";
    }

    class Error
    {
    public:
        const char *Code;
        const char *Status;
    };

    namespace Errors
    {
        static const Error InvalidRequest = {"ERR_INVALID_REQUEST", Statuses::_400};
        static const Error Unauthorized = {"ERR_UNAUTHORIZED", Statuses::_401};
        static const Error NotPermission = {"ERR_NOT_PERMISSION", Statuses::_403};
        static const Error NotFound = {"ERR_NOT_FOUND", Statuses::_404};
        static const Error ServerGeneric = {"ERR_SERVER_GENERIC", Statuses::_500};
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
        device::Controller *device;
        trigger::Controller *trigger;
        role::Controller *role;
        httpd_handle_t espServer;
        wl_handle_t fsHandle;
        httpd_uri_t frontURIHandler = {"/?*", Methods::GET, frontHandler};
        httpd_uri_t apiPostRegisterURIHandler = {"/api/register", Methods::POST, apiPostRegisterHandler};
        httpd_uri_t apiPostLoginURIHandler = {"/api/login", Methods::POST, apiPostLoginHandler};
        httpd_uri_t apiGetUsersURIHandler = {"/api/users", Methods::GET, apiGetUsersHandler};
        httpd_uri_t apiGetUserURIHandler = {"/api/users/*", Methods::GET, apiGetUserHandler};

    private:
        void start();
        void stop();
        esp_err_t sendFile(httpd_req_t *request, const char *path, const char *status);
        esp_err_t sendJSON(httpd_req_t *request, cJSON *json, const char *status);
        esp_err_t sendError(httpd_req_t *request, Error error, const char *message);
        esp_err_t recvJSON(httpd_req_t *request, cJSON **json);
        user::User *checkToken(httpd_req_t *request);
        const char *getPathParam(httpd_req_t *request);
        static void apFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void staFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static esp_err_t errorHandler(httpd_req_t *request, httpd_err_code_t error);
        static esp_err_t frontHandler(httpd_req_t *request);
        static esp_err_t apiPostRegisterHandler(httpd_req_t *request);
        static esp_err_t apiPostLoginHandler(httpd_req_t *request);
        static esp_err_t apiGetUsersHandler(httpd_req_t *request);
        static esp_err_t apiGetUserHandler(httpd_req_t *request);

    public:
        inline static Server *Instance;
        static Server *New(logger::Logger *logger, database::Database *database, provisioner::Provisioner *provisioner,
                           chron::Controller *chron, device::Transmitter *transmitter, device::Receiver *receiver,
                           user::Controller *user, device::Controller *device, trigger::Controller *trigger,
                           role::Controller *role);
    };
}