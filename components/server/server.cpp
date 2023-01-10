#include <errno.h>
#include <string.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "http_parser.h"
#include "cJSON.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_idf_version.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "logger.hpp"
#include "database.hpp"
#include "provisioner.hpp"
#include "chron.hpp"
#include "user.hpp"
#include "device.hpp"
#include "trigger.hpp"
#include "role.hpp"
#include "server.hpp"

namespace server
{
    Server *Server::New(logger::Logger *logger, database::Database *database, provisioner::Provisioner *provisioner,
                        chron::Controller *chron, device::Transmitter *transmitter, device::Receiver *receiver,
                        user::Controller *user, device::Controller *device, trigger::Controller *trigger,
                        role::Controller *role)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Server();

        // Inject dependencies
        Instance->logger = logger;
        Instance->database = database;
        Instance->provisioner = provisioner;
        Instance->chron = chron;
        Instance->transmitter = transmitter;
        Instance->receiver = receiver;
        Instance->user = user;
        Instance->device = device;
        Instance->trigger = trigger;
        Instance->role = role;

        Instance->espServer = NULL;

        // Register Wi-Fi station/softAP and LwIP event callbacks
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, Instance->staFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_AP_START, Instance->apFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_AP_STOP, Instance->apFunc, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, Instance->ipFunc, NULL, NULL));

        // Mount FAT filesystem from static partition
        esp_vfs_fat_mount_config_t cfg = {
            .format_if_mount_failed = false,
            .max_files = MAX_CLIENTS,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        };
        ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(ROOT, PARTITION, &cfg, &Instance->fsHandle));

        // Increase HTTPD log level, is too verbose
        esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
        esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
        esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

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
            .server_port = PORT,
            .ctrl_port = 32768,
            .max_open_sockets = MAX_CLIENTS,
            .max_uri_handlers = 30,
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
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_400_BAD_REQUEST, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_401_UNAUTHORIZED, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_403_FORBIDDEN, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_404_NOT_FOUND, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_405_METHOD_NOT_ALLOWED, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_408_REQ_TIMEOUT, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_411_LENGTH_REQUIRED, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_414_URI_TOO_LONG, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_500_INTERNAL_SERVER_ERROR, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_501_METHOD_NOT_IMPLEMENTED, this->errorHandler));
        ESP_ERROR_CHECK(httpd_register_err_handler(this->espServer, HTTPD_505_VERSION_NOT_SUPPORTED, this->errorHandler));

        // TODO: logger middleware

        // Register api handlers
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPostRegisterURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPostLoginURIHandler));

        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetUsersURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetUserURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPutUserURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiDeleteUserURIHandler));

        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetDevicesURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetDeviceURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPostDevicesURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPutDeviceURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiDeleteDeviceURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPostDeviceActuateURIHandler));

        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetTriggersURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetTriggerURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPostTriggersURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPutTriggerURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiDeleteTriggerURIHandler));

        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetRolesURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetRoleURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPostRolesURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPutRoleURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiDeleteRoleURIHandler));

        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetSystemInfoURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetSystemTimeURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiGetSystemWiFiURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiPutSystemWiFiURIHandler));
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->apiDeleteSystemResetURIHandler));

        // Register frontend handler, note that it has to be the last one to catch all other URLs
        ESP_ERROR_CHECK(httpd_register_uri_handler(this->espServer, &this->frontURIHandler));

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

    esp_err_t Server::sendFile(httpd_req_t *request, const char *path, const char *status)
    {
        esp_err_t err;

        // Set appropiate content type, encoding and status
        err = httpd_resp_set_hdr(request, Headers::ContentEncoding, ContentEncodings::GZIP);
        if (err != ESP_OK)
            return err;

        err = httpd_resp_set_status(request, status);
        if (err != ESP_OK)
            return err;

        err = httpd_resp_set_type(request, ContentTypes::TextHTML);
        if (err != ESP_OK)
            return err;

        // Open file
        FILE *file = fopen(path, "r");
        if (file == NULL)
        {
            return ESP_FAIL;
        }

        // Read and send file in chunks
        char chunk[1024];
        size_t size;
        do
        {
            size = fread(chunk, 1, sizeof(chunk), file);
            if (httpd_resp_send_chunk(request, chunk, size) != ESP_OK)
            {
                // Close file
                fclose(file);

                // On large responses client may reset the connection suddenly, ignore it and do not panic
                if (errno == ECONNRESET || errno == ENOTCONN)
                    return ESP_OK;

                // Abort sending file
                err = httpd_resp_send_chunk(request, NULL, 0);
                if (err != ESP_OK)
                    return err;

                return ESP_FAIL;
            }
        } while (size != 0);

        // Close file
        fclose(file);

        // Close connection for large responses to free the underlying socket instantly
        err = httpd_resp_set_hdr(request, Headers::Connection, "close");
        if (err != ESP_OK)
            return err;

        // Respond with an empty chunk to signal HTTP response completion
        err = httpd_resp_send_chunk(request, NULL, 0);
        if (err != ESP_OK)
            return err;

        return ESP_OK;
    }

    esp_err_t Server::sendJSON(httpd_req_t *request, cJSON *json, const char *status)
    {
        esp_err_t err;

        // Set appropiate content type and status
        err = httpd_resp_set_status(request, status);
        if (err != ESP_OK)
            return err;

        err = httpd_resp_set_type(request, ContentTypes::ApplicationJSON);
        if (err != ESP_OK)
            return err;

        // Send all body at once
        const char *body = cJSON_PrintUnformatted(json);
        err = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
        free((void *)body);
        if (err != ESP_OK)
            return err;

        return ESP_OK;
    }

    esp_err_t Server::sendError(httpd_req_t *request, Error error, const char *message)
    {
        esp_err_t err;

        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "code", error.Code);
        if (message != NULL)
            cJSON_AddStringToObject(root, "message", message);

        err = Instance->sendJSON(request, root, error.Status);
        cJSON_Delete(root);
        if (err != ESP_OK)
            return err;

        return ESP_OK;
    }

    esp_err_t Server::recvJSON(httpd_req_t *request, cJSON **json)
    {
        esp_err_t err;

        char body[MAX_REQUEST_CONTENT_SIZE + 1];

        // Check if request content fits in body buffer
        if (request->content_len > MAX_REQUEST_CONTENT_SIZE)
            return ESP_ERR_NO_MEM;

        // Trying to receive with no content generates an error
        if (request->content_len < 2)
        {
            *json = cJSON_Parse("{}");
            return ESP_OK;
        }

        err = httpd_req_recv(request, body, request->content_len);
        if (err <= 0)
        {
            // On large responses client may reset the connection suddenly, ignore it and do not panic
            if (err == HTTPD_SOCK_ERR_TIMEOUT)
                return ESP_OK;

            return ESP_FAIL;
        }

        // Ensure NULL-terminated string
        body[request->content_len] = '\0';

        // Parse request JSON content
        *json = cJSON_Parse(body);

        return ESP_OK;
    }

    user::User *Server::checkToken(httpd_req_t *request)
    {
        user::User *user = NULL;

        char header[MAX_REQUEST_HEADER_SIZE + 1];

        uint32_t size = httpd_req_get_hdr_value_len(request, Headers::Authorization);

        // Check if request headers fits in header buffer
        if (size > MAX_REQUEST_HEADER_SIZE)
            return NULL;

        // Authorization header should be present and at least user::TOKEN_SIZE
        if (size < user::TOKEN_SIZE)
            return NULL;

        // Read Authorization header
        ESP_ERROR_CHECK(httpd_req_get_hdr_value_str(request, Headers::Authorization, header, size + 1));

        // Search for name:token delimiter
        char *del = strchr(header, ':');

        // Replace delimiter with NULL to split the header
        *del = '\0';

        char *token = del + 1;
        char *name = header;

        // Get user
        user = Instance->user->Get(name);
        if (user == NULL)
            return NULL;

        // Check if tokens match
        if (strcmp(token, user->Token))
        {
            delete user;
            return NULL;
        }

        return user;
    }

    const char *Server::getPathParam(httpd_req_t *request)
    {
        // Search for the last path delimiter and return what's next
        return strdup(strrchr(request->uri, '/') + 1);
    }

    void Server::apFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Start HTTP server when Wi-Fi softAP has started
        if (id == WIFI_EVENT_AP_START)
            Instance->start();

        // Stop HTTP server when Wi-Fi softAP has stopped because underlying sockets are freed
        else // WIFI_EVENT_AP_STOP
            Instance->stop();
    }

    void Server::staFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Stop HTTP server when Wi-Fi station is disconnected because underlying sockets are freed
        // WIFI_EVENT_STA_DISCONNECTED
        Instance->stop();
    }

    void Server::ipFunc(void *args, esp_event_base_t base, int32_t id, void *data)
    {
        // Start HTTP server when IP is got
        // IP_EVENT_STA_GOT_IP
        Instance->start();
    }

    esp_err_t Server::errorHandler(httpd_req_t *request, httpd_err_code_t error)
    {
        const char *status;
        cJSON *root = cJSON_CreateObject();

        switch (error)
        {
        case HTTPD_400_BAD_REQUEST:
            status = Errors::InvalidRequest.Status;
            cJSON_AddStringToObject(root, "code", Errors::InvalidRequest.Code);
            break;

        case HTTPD_404_NOT_FOUND:
            status = Errors::NotFound.Status;
            cJSON_AddStringToObject(root, "code", Errors::NotFound.Code);
            break;

        default:
            status = Errors::ServerGeneric.Status;
            cJSON_AddStringToObject(root, "code", Errors::ServerGeneric.Code);
            break;
        }

        const char *body = cJSON_PrintUnformatted(root);

        ESP_ERROR_CHECK(httpd_resp_set_status(request, status));
        ESP_ERROR_CHECK(httpd_resp_set_type(request, ContentTypes::ApplicationJSON));

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

        free((void *)body);
        cJSON_Delete(root);

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

        return ESP_FAIL;
    }

    esp_err_t Server::frontHandler(httpd_req_t *request)
    {
        // On home path serve the Gzipped frontend from FATFS static partition
        if (!strcmp(request->uri, "/") || !strcmp(request->uri, ""))
        {
            // Set Cache Control header so clients load the frontend faster
            ESP_ERROR_CHECK(httpd_resp_set_hdr(request, Headers::CacheControl, "max-age=604800, stale-while-revalidate=86400, stale-if-error=86400"));
            ESP_ERROR_CHECK(Instance->sendFile(request, FRONTEND, Statuses::_200));

            return ESP_OK;
        }

        char *location = NULL;

        // If there are no users (ignoring the System default) redirect to onboarding page /#/onboarding
        if (Instance->user->Count() <= 1)
            ESP_ERROR_CHECK(httpd_resp_set_hdr(request, Headers::Location, "/#/onboarding"));
        // Otherwise redirect to the frontend hash-based navigation: /#/:URI
        else
        {
            location = (char *)malloc(strlen(request->uri) + 2 + 1);
            strcpy(location, "/#");
            ESP_ERROR_CHECK(httpd_resp_set_hdr(request, Headers::Location, strcat(location, request->uri)));
        }

        // If provisioner mode is AP use temporary redirects with content instead
        if (Instance->provisioner->GetMode() == WIFI_MODE_AP)
        {
            ESP_ERROR_CHECK(httpd_resp_set_status(request, Statuses::_302));
            // iOS requires content in the response to detect a captive portal
            ESP_ERROR_CHECK(httpd_resp_set_type(request, ContentTypes::TextHTML));
            ESP_ERROR_CHECK(httpd_resp_send(request, "<h1>Redirecting...</h1>", HTTPD_RESP_USE_STRLEN));
        }
        else
        {
            ESP_ERROR_CHECK(httpd_resp_set_status(request, Statuses::_301));
            ESP_ERROR_CHECK(httpd_resp_send(request, NULL, 0));
        }

        free((void *)location);

        return ESP_OK;
    }

    esp_err_t Server::apiPostRegisterHandler(httpd_req_t *request)
    {
        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Check if a user with the same name already exists
        user::User *exUser = Instance->user->Get(cJSON_GetObjectItem(reqJSON, "name")->valuestring);
        if (exUser != NULL)
        {
            cJSON_Delete(reqJSON);
            delete exUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "User already exists"));
            return ESP_FAIL;
        }

        // Generate new authentication token
        char token[user::TOKEN_SIZE + 1];
        Instance->user->GenerateToken(token);

        // Create new user
        user::User newUser(
            cJSON_GetObjectItem(reqJSON, "name")->valuestring,
            cJSON_GetObjectItem(reqJSON, "password")->valuestring,
            token,
            Instance->user->Count() > 1 ? role::System::Guest.Name : role::System::Admin.Name,
            cJSON_GetObjectItem(reqJSON, "emoji")->valuestring,
            Instance->chron->Now());

        cJSON_Delete(reqJSON);

        Instance->user->Set(&newUser);

        // Send response JSON
        cJSON *resJSON = newUser.JSON();
        cJSON_DeleteItemFromObject(resJSON, "password");
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPostLoginHandler(httpd_req_t *request)
    {
        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Ensure not logging as the default system user
        if (user::System::System.Equals(cJSON_GetObjectItem(reqJSON, "name")->valuestring))
        {
            cJSON_Delete(reqJSON);
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot log as system users"));
            return ESP_FAIL;
        }

        // Get user
        user::User *user = Instance->user->Get(cJSON_GetObjectItem(reqJSON, "name")->valuestring);
        if (user == NULL)
        {
            cJSON_Delete(reqJSON);
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "User doesn't exist"));
            return ESP_FAIL;
        }

        // Check if passwords match
        if (strcmp(cJSON_GetObjectItem(reqJSON, "password")->valuestring, user->Password))
        {
            cJSON_Delete(reqJSON);
            delete user;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Passwords don't match"));
            return ESP_FAIL;
        }

        cJSON_Delete(reqJSON);

        // Generate new authentication token
        char token[user::TOKEN_SIZE + 1];
        Instance->user->GenerateToken(token);
        free((void *)user->Token);
        user->Token = strdup(token);

        // Save user
        Instance->user->Set(user);

        // Send response JSON
        cJSON *resJSON = user->JSON();
        delete user;
        cJSON_DeleteItemFromObject(resJSON, "password");
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetUsersHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get all users
        uint32_t size;
        user::User *users = Instance->user->List(&size);

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        cJSON *usersJSON = cJSON_AddArrayToObject(resJSON, "users");

        for (int i = 0; i < size; i++)
        {
            cJSON *userJSON = users[i].JSON();
            cJSON_DeleteItemFromObject(userJSON, "password");
            cJSON_DeleteItemFromObject(userJSON, "token");
            cJSON_AddItemToArray(usersJSON, userJSON);
        }

        delete[] users;

        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetUserHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get user
        user::User *user = Instance->user->Get(name);
        free((void *)name);
        if (user == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "User doesn't exist"));
            return ESP_FAIL;
        }

        // Send response JSON
        cJSON *resJSON = user->JSON();
        delete user;
        cJSON_DeleteItemFromObject(resJSON, "password");
        cJSON_DeleteItemFromObject(resJSON, "token");
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPutUserHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Ensure not modifying the default system user
        if (user::System::System.Equals(name))
        {
            delete reqUser;
            free((void *)name);
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot modify system users"));
            return ESP_FAIL;
        }

        // Get user
        user::User *user = Instance->user->Get(name);
        free((void *)name);
        if (user == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "User doesn't exist"));
            return ESP_FAIL;
        }

        // Check if it is the requesting user or it is an admin
        if (!reqUser->Equals(user) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete reqUser;
            delete user;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot modify another user"));
            return ESP_FAIL;
        }

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Update password if present
        if (cJSON_GetObjectItem(reqJSON, "password") != NULL)
        {
            free((void *)user->Password);
            user->Password = strdup(cJSON_GetObjectItem(reqJSON, "password")->valuestring);
        }

        // Update emoji if present
        if (cJSON_GetObjectItem(reqJSON, "emoji") != NULL)
        {
            free((void *)user->Emoji);
            user->Emoji = strdup(cJSON_GetObjectItem(reqJSON, "emoji")->valuestring);
        }

        // Update role if present
        if (cJSON_GetObjectItem(reqJSON, "role") != NULL)
        {
            // Check if the requesting user is an admin
            if (!Instance->user->Belongs(reqUser, &role::System::Admin))
            {
                delete reqUser;
                delete user;
                cJSON_Delete(reqJSON);
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot change role of another user"));
                return ESP_FAIL;
            }

            // Check if role exists
            role::Role *role = Instance->role->Get(cJSON_GetObjectItem(reqJSON, "role")->valuestring);
            if (role == NULL)
            {
                delete reqUser;
                delete user;
                cJSON_Delete(reqJSON);
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Role doesn't exist"));
                return ESP_FAIL;
            }

            free((void *)user->Role);
            user->Role = strdup(role->Name);
            delete role;
        }

        delete reqUser;
        cJSON_Delete(reqJSON);

        // Save user
        Instance->user->Set(user);

        // Send response JSON
        cJSON *resJSON = user->JSON();
        delete user;
        cJSON_DeleteItemFromObject(resJSON, "password");
        cJSON_DeleteItemFromObject(resJSON, "token");
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiDeleteUserHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Ensure not deleting the default system user
        if (user::System::System.Equals(name))
        {
            delete reqUser;
            free((void *)name);
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete system users"));
            return ESP_FAIL;
        }

        // Get user
        user::User *user = Instance->user->Get(name);
        free((void *)name);
        if (user == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "User doesn't exist"));
            return ESP_FAIL;
        }

        // Check if it is the requesting user or it is an admin
        if (!reqUser->Equals(user) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete reqUser;
            delete user;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete another user"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Delete user
        Instance->user->Delete(user->Name);

        // Send response JSON
        cJSON *resJSON = user->JSON();
        delete user;
        cJSON_DeleteItemFromObject(resJSON, "password");
        cJSON_DeleteItemFromObject(resJSON, "token");
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetDevicesHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get all devices
        uint32_t size;
        device::Device *devices = Instance->device->List(&size);

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        cJSON *devicesJSON = cJSON_AddArrayToObject(resJSON, "devices");

        // Filter devices depending if the requesting user role includes it or it is an admin
        for (int i = 0; i < size; i++)
            if (Instance->role->Includes(reqUser->Role, &devices[i]) || Instance->user->Belongs(reqUser, &role::System::Admin))
                cJSON_AddItemToArray(devicesJSON, devices[i].JSON());

        delete reqUser;
        delete[] devices;

        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetDeviceHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get device
        device::Device *device = Instance->device->GetByName(name);
        free((void *)name);
        if (device == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device doesn't exist"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the device or it is an admin
        if (!Instance->role->Includes(reqUser->Role, device) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete device;
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot get device"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Send response JSON
        cJSON *resJSON = device->JSON();
        delete device;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPostDevicesHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Check if a device with the same name already exists
        device::Device *exDevice = Instance->device->GetByName(cJSON_GetObjectItem(reqJSON, "name")->valuestring);
        if (exDevice != NULL)
        {
            delete exDevice;
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device already exists"));
            return ESP_FAIL;
        }

        // Check if the type is valid
        if (strcmp(cJSON_GetObjectItem(reqJSON, "type")->valuestring, device::Types::Sensor) &&
            strcmp(cJSON_GetObjectItem(reqJSON, "type")->valuestring, device::Types::Actuator))
        {
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Type is invalid"));
            return ESP_FAIL;
        }

        // Check if the subtype is valid
        device::Context context;
        cJSON *reqContext = cJSON_GetObjectItem(reqJSON, "context");

        if (!strcmp(cJSON_GetObjectItem(reqJSON, "subtype")->valuestring, device::Subtypes::Button))
        {
            context.Button.Command = cJSON_GetObjectItem(reqContext, "command")->valuestring;
            context.Button.Emoji = cJSON_GetObjectItem(reqContext, "emoji")->valuestring;
        }
        else if (!strcmp(cJSON_GetObjectItem(reqJSON, "subtype")->valuestring, device::Subtypes::Bistate))
        {
            context.Bistate.Identifier1 = cJSON_GetObjectItem(reqContext, "identifier1")->valuestring;
            context.Bistate.Emoji1 = cJSON_GetObjectItem(reqContext, "emoji1")->valuestring;
            context.Bistate.Identifier2 = cJSON_GetObjectItem(reqContext, "identifier2")->valuestring;
            context.Bistate.Emoji2 = cJSON_GetObjectItem(reqContext, "emoji2")->valuestring;
            context.Bistate.State = 0;
        }
        else
        {
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Subtype is invalid"));
            return ESP_FAIL;
        }

        // Create new device
        device::Device newDevice(
            cJSON_GetObjectItem(reqJSON, "name")->valuestring,
            cJSON_GetObjectItem(reqJSON, "type")->valuestring,
            cJSON_GetObjectItem(reqJSON, "subtype")->valuestring,
            cJSON_GetObjectItem(reqJSON, "protocol")->valueint,
            context,
            cJSON_GetObjectItem(reqJSON, "emoji")->valuestring,
            reqUser->Name,
            Instance->chron->Now());

        cJSON_Delete(reqJSON);
        delete reqUser;

        Instance->device->Set(&newDevice);

        // Send response JSON
        cJSON *resJSON = newDevice.JSON();
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPutDeviceHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get device
        device::Device *device = Instance->device->GetByName(name);
        free((void *)name);
        if (device == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device doesn't exist"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the device or it is an admin
        if (!Instance->role->Includes(reqUser->Role, device) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete device;
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot modify device"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Update protocol if present
        if (cJSON_GetObjectItem(reqJSON, "protocol") != NULL)
        {
            device->Protocol = cJSON_GetObjectItem(reqJSON, "protocol")->valueint;
        }

        // Update context if present
        if (cJSON_GetObjectItem(reqJSON, "context") != NULL)
        {
            cJSON *reqContext = cJSON_GetObjectItem(reqJSON, "context");

            if (!strcmp(device->Subtype, device::Subtypes::Button))
            {
                free((void *)device->Context.Button.Command);
                free((void *)device->Context.Button.Emoji);
                device->Context.Button.Command = strdup(cJSON_GetObjectItem(reqContext, "command")->valuestring);
                device->Context.Button.Emoji = strdup(cJSON_GetObjectItem(reqContext, "emoji")->valuestring);
            }
            else if (!strcmp(device->Subtype, device::Subtypes::Bistate))
            {
                free((void *)device->Context.Bistate.Identifier1);
                free((void *)device->Context.Bistate.Emoji1);
                free((void *)device->Context.Bistate.Identifier2);
                free((void *)device->Context.Bistate.Emoji2);
                device->Context.Bistate.Identifier1 = strdup(cJSON_GetObjectItem(reqContext, "identifier1")->valuestring);
                device->Context.Bistate.Emoji1 = strdup(cJSON_GetObjectItem(reqContext, "emoji1")->valuestring);
                device->Context.Bistate.Identifier2 = strdup(cJSON_GetObjectItem(reqContext, "identifier2")->valuestring);
                device->Context.Bistate.Emoji2 = strdup(cJSON_GetObjectItem(reqContext, "emoji2")->valuestring);
            }
        }

        // Update emoji if present
        if (cJSON_GetObjectItem(reqJSON, "emoji") != NULL)
        {
            free((void *)device->Emoji);
            device->Emoji = strdup(cJSON_GetObjectItem(reqJSON, "emoji")->valuestring);
        }

        cJSON_Delete(reqJSON);

        // Save device
        Instance->device->Set(device);

        // Send response JSON
        cJSON *resJSON = device->JSON();
        delete device;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiDeleteDeviceHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get device
        device::Device *device = Instance->device->GetByName(name);
        free((void *)name);
        if (device == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device doesn't exist"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the device or it is an admin
        if (!Instance->role->Includes(reqUser->Role, device) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete device;
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete device"));
            return ESP_FAIL;
        }

        delete reqUser;

        // If device is an actuator delete all device's triggers
        if (!strcmp(device->Type, device::Types::Actuator))
            Instance->trigger->DeleteByActuator(device->Name);

        // Remove device from all roles
        Instance->role->RemoveDeviceFromAllRoles(device->Name);

        // Delete device
        Instance->device->Delete(device->Name);

        // Send response JSON
        cJSON *resJSON = device->JSON();
        delete device;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPostDeviceActuateHandler(httpd_req_t *request)
    {
        return ESP_OK;
    }

    esp_err_t Server::apiGetTriggersHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get all triggers
        uint32_t size;
        trigger::Trigger *triggers = Instance->trigger->List(&size);

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        cJSON *triggersJSON = cJSON_AddArrayToObject(resJSON, "triggers");

        // Filter triggers depending if the requesting user role includes the triggered actuator or it is an admin
        for (int i = 0; i < size; i++)
            if (Instance->role->Includes(reqUser->Role, triggers[i].Actuator) || Instance->user->Belongs(reqUser, &role::System::Admin))
                cJSON_AddItemToArray(triggersJSON, triggers[i].JSON());

        delete reqUser;
        delete[] triggers;

        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetTriggerHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get trigger
        trigger::Trigger *trigger = Instance->trigger->Get(name);
        free((void *)name);
        if (trigger == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Trigger doesn't exist"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the triggered actuator or it is an admin
        if (!Instance->role->Includes(reqUser->Role, trigger->Actuator) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete trigger;
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot get trigger"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Send response JSON
        cJSON *resJSON = trigger->JSON();
        delete trigger;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPostTriggersHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Check if a trigger with the same name already exists
        trigger::Trigger *exTrigger = Instance->trigger->Get(cJSON_GetObjectItem(reqJSON, "name")->valuestring);
        if (exTrigger != NULL)
        {
            delete exTrigger;
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Trigger already exists"));
            return ESP_FAIL;
        }

        // Check if triggered actuator exists
        device::Device *actuator = Instance->device->GetByName(cJSON_GetObjectItem(reqJSON, "actuator")->valuestring);
        if (actuator == NULL)
        {
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Actuator doesn't exist"));
            return ESP_FAIL;
        }

        // Check if triggered device is an actuator
        if (strcmp(actuator->Type, device::Types::Actuator))
        {
            delete actuator;
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device is not an actuator"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the actuator or it is an admin
        if (!Instance->role->Includes(reqUser->Role, actuator) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete actuator;
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot create trigger"));
            return ESP_FAIL;
        }

        // Check if the schedule is valid
        if (!Instance->trigger->IsScheduleValid(cJSON_GetObjectItem(reqJSON, "schedule")->valuestring))
        {
            delete actuator;
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Schedule is invalid"));
            return ESP_FAIL;
        }

        // Create new trigger
        trigger::Trigger newTrigger(
            cJSON_GetObjectItem(reqJSON, "name")->valuestring,
            actuator->Name,
            cJSON_GetObjectItem(reqJSON, "schedule")->valuestring,
            cJSON_GetObjectItem(reqJSON, "emoji")->valuestring,
            reqUser->Name,
            Instance->chron->Now());

        delete actuator;
        cJSON_Delete(reqJSON);
        delete reqUser;

        Instance->trigger->Set(&newTrigger);

        // Send response JSON
        cJSON *resJSON = newTrigger.JSON();
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPutTriggerHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get trigger
        trigger::Trigger *trigger = Instance->trigger->Get(name);
        free((void *)name);
        if (trigger == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Trigger doesn't exist"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the triggered actuator or it is an admin
        if (!Instance->role->Includes(reqUser->Role, trigger->Actuator) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete trigger;
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot modify trigger"));
            return ESP_FAIL;
        }

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Update actuator if present
        if (cJSON_GetObjectItem(reqJSON, "actuator") != NULL)
        {
            // Check if triggered actuator exists
            device::Device *actuator = Instance->device->GetByName(cJSON_GetObjectItem(reqJSON, "actuator")->valuestring);
            if (actuator == NULL)
            {
                cJSON_Delete(reqJSON);
                delete trigger;
                delete reqUser;
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Actuator doesn't exist"));
                return ESP_FAIL;
            }

            // Check if triggered device is an actuator
            if (strcmp(actuator->Type, device::Types::Actuator))
            {
                delete actuator;
                cJSON_Delete(reqJSON);
                delete trigger;
                delete reqUser;
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device is not an actuator"));
                return ESP_FAIL;
            }

            // Check if the requesting user role includes the actuator or it is an admin
            if (!Instance->role->Includes(reqUser->Role, actuator) && !Instance->user->Belongs(reqUser, &role::System::Admin))
            {
                delete actuator;
                cJSON_Delete(reqJSON);
                delete trigger;
                delete reqUser;
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot create trigger"));
                return ESP_FAIL;
            }

            free((void *)trigger->Actuator);
            trigger->Actuator = strdup(actuator->Name);
            delete actuator;
        }

        // Update schedule if present
        if (cJSON_GetObjectItem(reqJSON, "schedule") != NULL)
        {
            // Check if the schedule is valid
            if (!Instance->trigger->IsScheduleValid(cJSON_GetObjectItem(reqJSON, "schedule")->valuestring))
            {
                cJSON_Delete(reqJSON);
                delete trigger;
                delete reqUser;
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Schedule is invalid"));
                return ESP_FAIL;
            }

            free((void *)trigger->Schedule);
            trigger->Schedule = strdup(cJSON_GetObjectItem(reqJSON, "schedule")->valuestring);
        }

        // Update emoji if present
        if (cJSON_GetObjectItem(reqJSON, "emoji") != NULL)
        {
            free((void *)trigger->Emoji);
            trigger->Emoji = strdup(cJSON_GetObjectItem(reqJSON, "emoji")->valuestring);
        }

        cJSON_Delete(reqJSON);
        delete reqUser;

        // Save trigger
        Instance->trigger->Set(trigger);

        // Send response JSON
        cJSON *resJSON = trigger->JSON();
        delete trigger;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiDeleteTriggerHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get trigger
        trigger::Trigger *trigger = Instance->trigger->Get(name);
        free((void *)name);
        if (trigger == NULL)
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Trigger doesn't exist"));
            return ESP_FAIL;
        }

        // Check if the requesting user role includes the triggered actuator or it is an admin
        if (!Instance->role->Includes(reqUser->Role, trigger->Actuator) && !Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete trigger;
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete trigger"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Delete trigger
        Instance->trigger->DeleteByName(trigger->Name);

        // Send response JSON
        cJSON *resJSON = trigger->JSON();
        delete trigger;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetRolesHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get all roles
        uint32_t size;
        role::Role *roles = Instance->role->List(&size);

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        cJSON *rolesJSON = cJSON_AddArrayToObject(resJSON, "roles");

        for (int i = 0; i < size; i++)
            cJSON_AddItemToArray(rolesJSON, roles[i].JSON());

        delete[] roles;

        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetRoleHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Get role
        role::Role *role = Instance->role->Get(name);
        free((void *)name);
        if (role == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Role doesn't exist"));
            return ESP_FAIL;
        }

        // Send response JSON
        cJSON *resJSON = role->JSON();
        delete role;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPostRolesHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Check if the requesting user is an admin
        if (!Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot create role"));
            return ESP_FAIL;
        }

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Check if a role with the same name already exists
        role::Role *exRole = Instance->role->Get(cJSON_GetObjectItem(reqJSON, "name")->valuestring);
        if (exRole != NULL)
        {
            delete exRole;
            cJSON_Delete(reqJSON);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Role already exists"));
            return ESP_FAIL;
        }

        // Check if included devices exist
        cJSON *reqDevices = cJSON_GetObjectItem(reqJSON, "devices");
        int size = cJSON_GetArraySize(reqDevices);
        const char **devices = (const char **)malloc((size + 1) * sizeof(char *));

        for (int i = 0; i < size; i++)
        {
            device::Device *device = Instance->device->GetByName(cJSON_GetArrayItem(reqDevices, i)->valuestring);
            if (device == NULL)
            {
                free((void *)devices);
                cJSON_Delete(reqJSON);
                delete reqUser;
                ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device doesn't exist"));
                return ESP_FAIL;
            }

            devices[i] = strdup(device->Name);
            delete device;
        }
        devices[size] = NULL;

        // Create new role
        role::Role newRole(
            cJSON_GetObjectItem(reqJSON, "name")->valuestring,
            devices,
            cJSON_GetObjectItem(reqJSON, "emoji")->valuestring,
            reqUser->Name,
            Instance->chron->Now());

        free((void *)devices);
        cJSON_Delete(reqJSON);
        delete reqUser;

        Instance->role->Set(&newRole);

        // Send response JSON
        cJSON *resJSON = newRole.JSON();
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPutRoleHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Ensure not modifying the default system roles
        if (role::System::Admin.Equals(name) || role::System::Guest.Equals(name))
        {
            free((void *)name);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot modify system roles"));
            return ESP_FAIL;
        }

        // Check if the requesting user is an admin
        if (!Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            free((void *)name);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot modify role"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get role
        role::Role *role = Instance->role->Get(name);
        free((void *)name);
        if (role == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Role doesn't exist"));
            return ESP_FAIL;
        }

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        const char **devices = NULL;
        // Update devices if present
        if (cJSON_GetObjectItem(reqJSON, "devices") != NULL)
        {
            // Check if included devices exist
            cJSON *reqDevices = cJSON_GetObjectItem(reqJSON, "devices");
            int size = cJSON_GetArraySize(reqDevices);
            devices = (const char **)malloc((size + 1) * sizeof(char *));

            for (int i = 0; i < size; i++)
            {
                device::Device *device = Instance->device->GetByName(cJSON_GetArrayItem(reqDevices, i)->valuestring);
                if (device == NULL)
                {
                    free((void *)devices);
                    cJSON_Delete(reqJSON);
                    ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Device doesn't exist"));
                    return ESP_FAIL;
                }

                devices[i] = strdup(device->Name);
                delete device;
            }
            devices[size] = NULL;

            for (int i = 0; role->Devices[i] != NULL; i++)
                free((void *)role->Devices[i]);
            free((void *)role->Devices);

            // Passing freeing ownership
            role->Devices = devices;
        }

        // Update emoji if present
        if (cJSON_GetObjectItem(reqJSON, "emoji") != NULL)
        {
            free((void *)role->Emoji);
            role->Emoji = strdup(cJSON_GetObjectItem(reqJSON, "emoji")->valuestring);
        }

        cJSON_Delete(reqJSON);

        // Save role
        Instance->role->Set(role);

        // Send response JSON
        cJSON *resJSON = role->JSON();
        delete role;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiDeleteRoleHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Get name path param
        const char *name = Instance->getPathParam(request);

        // Ensure not deleting the default system roles
        if (role::System::Admin.Equals(name) || role::System::Guest.Equals(name))
        {
            free((void *)name);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete system roles"));
            return ESP_FAIL;
        }

        // Check if the requesting user is an admin
        if (!Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            free((void *)name);
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete role"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get role
        role::Role *role = Instance->role->Get(name);
        free((void *)name);
        if (role == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::InvalidRequest, "Role doesn't exist"));
            return ESP_FAIL;
        }

        // Check if role is being used
        if (Instance->user->ExistsWithRole(role->Name))
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot delete used role"));
            return ESP_FAIL;
        }

        // Delete role
        Instance->role->Delete(role->Name);

        // Send response JSON
        cJSON *resJSON = role->JSON();
        delete role;
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetSystemInfoHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        cJSON *resJSON = cJSON_CreateObject();

        // Get chip info
        esp_chip_info_t chipInfo;
        esp_chip_info(&chipInfo);
        uint8_t mac;
        ESP_ERROR_CHECK(esp_read_mac(&mac, ESP_MAC_WIFI_SOFTAP));

        // TODO: Put all chip info like: https://github.com/espressif/esp-idf/blob/master/examples/get-started/hello_world/main/hello_world_main.c
        cJSON *chipJSON = cJSON_AddObjectToObject(resJSON, "chip");
        cJSON *wifiJSON = cJSON_AddObjectToObject(chipJSON, "wifi");
        cJSON_AddStringToObject(wifiJSON, "name", provisioner::AP_SSID);
        cJSON_AddStringToObject(wifiJSON, "password", provisioner::AP_PASSWORD);
        // TODO: Put MAC using sprintf, MACSTR and MAC2STR
        cJSON_AddStringToObject(wifiJSON, "mac", "");

        // Get firmware info
        const esp_app_desc_t *appDesc = esp_app_get_description();

        cJSON *firmwareJSON = cJSON_AddObjectToObject(resJSON, "firmware");
        cJSON *sdkJSON = cJSON_AddObjectToObject(firmwareJSON, "sdk");
        cJSON_AddStringToObject(sdkJSON, "name", "ESP-IDF");
        cJSON_AddStringToObject(sdkJSON, "version", appDesc->idf_ver);
        cJSON *appJSON = cJSON_AddObjectToObject(firmwareJSON, "app");
        cJSON_AddStringToObject(appJSON, "name", appDesc->project_name);
        cJSON_AddStringToObject(appJSON, "version", appDesc->version);
        cJSON_AddStringToObject(appJSON, "date", appDesc->date);
        cJSON_AddStringToObject(appJSON, "time", appDesc->time);

        // Get tasks info
        char taskInfoList[1024];
        vTaskList(taskInfoList);

        // TODO: Put all task info like: https://github.com/Neoxelox/bow/blob/8ac2ed5170df906e88818264e75fa164f8d736b2/main/main.cpp
        cJSON *tasksJSON = cJSON_AddObjectToObject(resJSON, "tasks");

        // Get resources info (esp_get_free_heap_size)
        uint32_t totalRAM;
        uint32_t usedRAM;
        uint32_t totalROM;
        uint32_t usedROM;
        // ESP_ERROR_CHECK(esp_flash_get_size(NULL, &totalROM));
        // usedRAM = totalRAM - esp_get_free_heap_size();

        // TODO: Put used/total RAM/ROM
        cJSON *resourcesJSON = cJSON_AddObjectToObject(resJSON, "resources");

        // Get time info
        cJSON *timeJSON = cJSON_AddObjectToObject(resJSON, "time");
        cJSON_AddStringToObject(timeJSON, "server", chron::NTP_SERVER_ADDRESS);
        cJSON_AddStringToObject(timeJSON, "zone", chron::TIME_ZONE);

        // Get database info
        nvs_stats_t databaseInfo;
        Instance->database->Info(&databaseInfo);

        cJSON *databaseJSON = cJSON_AddObjectToObject(resJSON, "database");
        cJSON_AddNumberToObject(databaseJSON, "total", databaseInfo.total_entries);
        cJSON_AddNumberToObject(databaseJSON, "used", databaseInfo.used_entries);

        // Send response JSON
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetSystemTimeHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        // Get current time
        time_t now = Instance->chron->Now();

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        cJSON_AddNumberToObject(resJSON, "time", now);
        cJSON_AddStringToObject(resJSON, "zone", chron::TIME_ZONE);
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiGetSystemWifiHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        delete reqUser;

        cJSON *resJSON = cJSON_CreateObject();

        // Get current Wi-Fi network
        provisioner::Credentials *creds = Instance->provisioner->GetCreds();

        if (creds != NULL && Instance->provisioner->GetMode() == WIFI_MODE_STA)
        {
            wifi_ap_record_t info;
            Instance->provisioner->GetCurrent(&info);

            cJSON *currentJSON = cJSON_AddObjectToObject(resJSON, "current");
            cJSON_AddStringToObject(currentJSON, "name", (char *)info.ssid);
            cJSON_AddNumberToObject(currentJSON, "strength", info.rssi);
            // TODO: Add security info
            cJSON_AddStringToObject(currentJSON, "security", "");
            // TODO: Add IP info
            cJSON_AddStringToObject(currentJSON, "address", "");
            cJSON_AddStringToObject(currentJSON, "netmask", "");
            cJSON_AddStringToObject(currentJSON, "gateway", "");
            // TODO: Add MAC info using sprintf, MACSTR and MAC2STR
            cJSON_AddStringToObject(currentJSON, "mac", "");
        }
        else
            cJSON_AddNullToObject(resJSON, "current");

        delete creds;

        // TODO: Get available Wi-Fi networks
        cJSON *availableJSON = cJSON_AddArrayToObject(resJSON, "available");

        // Send response JSON
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        return ESP_OK;
    }

    esp_err_t Server::apiPutSystemWifiHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Check if the requesting user is an admin
        if (!Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot change system wifi"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Bind request JSON
        cJSON *reqJSON = NULL;
        ESP_ERROR_CHECK(Instance->recvJSON(request, &reqJSON));

        // Create new credentials
        provisioner::Credentials creds(cJSON_GetObjectItem(reqJSON, "name")->valuestring,
                                       cJSON_GetObjectItem(reqJSON, "password")->valuestring);
        cJSON_Delete(reqJSON);

        // Save credentials
        Instance->provisioner->SetCreds(&creds);

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        cJSON_AddStringToObject(resJSON, "name", creds.SSID);
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        // Try connecting to the new target Wi-Fi network
        Instance->provisioner->Retry();

        return ESP_OK;
    }

    esp_err_t Server::apiDeleteSystemResetHandler(httpd_req_t *request)
    {
        // Authenticate request user
        user::User *reqUser = Instance->checkToken(request);
        if (reqUser == NULL)
        {
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::Unauthorized, NULL));
            return ESP_FAIL;
        }

        // Check if the requesting user is an admin
        if (!Instance->user->Belongs(reqUser, &role::System::Admin))
        {
            delete reqUser;
            ESP_ERROR_CHECK(Instance->sendError(request, Errors::NoPermission, "Cannot reset system"));
            return ESP_FAIL;
        }

        delete reqUser;

        // Send response JSON
        cJSON *resJSON = cJSON_CreateObject();
        ESP_ERROR_CHECK(Instance->sendJSON(request, resJSON, Statuses::_200));
        cJSON_Delete(resJSON);

        // Schedule database reset
        Instance->database->ScheduleReset();

        // Perform hardware reset to force database reset
        esp_restart();

        return ESP_OK;
    }
}
