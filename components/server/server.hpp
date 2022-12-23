#pragma once

#include "esp_event.h"
#include "esp_http_server.h"
#include "logger.hpp"
#include "provisioner.hpp"

namespace server
{
    static const char *TAG = "server";

    class Server
    {
    private:
        logger::Logger *logger;
        provisioner::Provisioner *provisioner;
        httpd_handle_t espServer;

    private:
        void start();
        void stop();
        static void wifiFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static void ipFunc(void *args, esp_event_base_t base, int32_t id, void *data);
        static esp_err_t errHandler(httpd_req_t *request, httpd_err_code_t error);
        static esp_err_t getHandler(httpd_req_t *request);

    public:
        inline static Server *Instance;
        static Server *New(logger::Logger *logger, provisioner::Provisioner *provisioner);
    };
}