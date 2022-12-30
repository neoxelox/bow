#include <string.h>
#include "esp_err.h"
#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"
#include "device.hpp"

namespace device
{
    Device::Device()
    {
        this->Name = NULL;
        this->Type = NULL;
        this->Subtype = NULL;
        this->Protocol = 0;
        memset(&this->Context, 0, sizeof(union Context));
        this->Emoji = NULL;
        this->Creator = NULL;
        this->CreatedAt = 0;
    }

    Device::Device(const char *name, const char *type, const char *subtype,
                   uint8_t protocol, union Context context, const char *emoji,
                   const char *creator, time_t createdAt)
    {
        this->Name = strdup(name);
        this->Type = strdup(type);
        this->Subtype = strdup(subtype);
        this->Protocol = protocol;

        if (!strcmp(subtype, Subtypes::Button))
        {
            this->Context.Button.Command = strdup(context.Button.Command);
            this->Context.Button.Emoji = strdup(context.Button.Emoji);
        }
        else if (!strcmp(subtype, Subtypes::Bistate))
        {
            this->Context.Bistate.Identifier1 = strdup(context.Bistate.Identifier1);
            this->Context.Bistate.Emoji1 = strdup(context.Bistate.Emoji1);
            this->Context.Bistate.Identifier2 = strdup(context.Bistate.Identifier2);
            this->Context.Bistate.Emoji2 = strdup(context.Bistate.Emoji2);
            this->Context.Bistate.LastIdentifier = strdup(context.Bistate.LastIdentifier);
        }
        else
            memset(&this->Context, 0, sizeof(union Context));

        this->Emoji = strdup(emoji);
        this->Creator = strdup(creator);
        this->CreatedAt = createdAt;
    }

    Device::Device(cJSON *src)
    {
        this->Name = strdup(cJSON_GetObjectItem(src, "name")->valuestring);
        this->Type = strdup(cJSON_GetObjectItem(src, "type")->valuestring);
        this->Subtype = strdup(cJSON_GetObjectItem(src, "subtype")->valuestring);
        this->Protocol = cJSON_GetObjectItem(src, "protocol")->valueint;

        cJSON *context = cJSON_GetObjectItem(src, "context");
        if (!strcmp(this->Subtype, Subtypes::Button))
        {
            this->Context.Button.Command = strdup(cJSON_GetObjectItem(context, "command")->valuestring);
            this->Context.Button.Emoji = strdup(cJSON_GetObjectItem(context, "emoji")->valuestring);
        }
        else if (!strcmp(this->Subtype, Subtypes::Bistate))
        {
            this->Context.Bistate.Identifier1 = strdup(cJSON_GetObjectItem(context, "identifier1")->valuestring);
            this->Context.Bistate.Emoji1 = strdup(cJSON_GetObjectItem(context, "emoji1")->valuestring);
            this->Context.Bistate.Identifier2 = strdup(cJSON_GetObjectItem(context, "identifier2")->valuestring);
            this->Context.Bistate.Emoji2 = strdup(cJSON_GetObjectItem(context, "emoji2")->valuestring);
            this->Context.Bistate.LastIdentifier = strdup(cJSON_GetObjectItem(context, "last_identifier")->valuestring);
        }
        else
            memset(&this->Context, 0, sizeof(union Context));

        this->Emoji = strdup(cJSON_GetObjectItem(src, "emoji")->valuestring);
        this->Creator = strdup(cJSON_GetObjectItem(src, "creator")->valuestring);
        this->CreatedAt = cJSON_GetObjectItem(src, "created_at")->valueint;
    }

    Device::~Device()
    {
        free((void *)this->Name);
        free((void *)this->Type);

        if (!strcmp(this->Subtype, Subtypes::Button))
        {
            free((void *)this->Context.Button.Command);
            free((void *)this->Context.Button.Emoji);
        }
        else if (!strcmp(this->Subtype, Subtypes::Bistate))
        {
            free((void *)this->Context.Bistate.Identifier1);
            free((void *)this->Context.Bistate.Emoji1);
            free((void *)this->Context.Bistate.Identifier2);
            free((void *)this->Context.Bistate.Emoji2);
            free((void *)this->Context.Bistate.LastIdentifier);
        }
        else
            memset(&this->Context, 0, sizeof(union Context));

        free((void *)this->Subtype);
        free((void *)this->Emoji);
        free((void *)this->Creator);
    }

    Device &Device::operator=(const Device &other)
    {
        if (this == &other)
            return *this;

        free((void *)this->Name);
        free((void *)this->Type);

        if (!strcmp(this->Subtype, Subtypes::Button))
        {
            free((void *)this->Context.Button.Command);
            free((void *)this->Context.Button.Emoji);
        }
        else if (!strcmp(this->Subtype, Subtypes::Bistate))
        {
            free((void *)this->Context.Bistate.Identifier1);
            free((void *)this->Context.Bistate.Emoji1);
            free((void *)this->Context.Bistate.Identifier2);
            free((void *)this->Context.Bistate.Emoji2);
            free((void *)this->Context.Bistate.LastIdentifier);
        }
        else
            memset(&this->Context, 0, sizeof(union Context));

        free((void *)this->Subtype);
        free((void *)this->Emoji);
        free((void *)this->Creator);

        this->Name = strdup(other.Name);
        this->Type = strdup(other.Type);
        this->Subtype = strdup(other.Subtype);
        this->Protocol = other.Protocol;

        if (!strcmp(other.Subtype, Subtypes::Button))
        {
            this->Context.Button.Command = strdup(other.Context.Button.Command);
            this->Context.Button.Emoji = strdup(other.Context.Button.Emoji);
        }
        else if (!strcmp(other.Subtype, Subtypes::Bistate))
        {
            this->Context.Bistate.Identifier1 = strdup(other.Context.Bistate.Identifier1);
            this->Context.Bistate.Emoji1 = strdup(other.Context.Bistate.Emoji1);
            this->Context.Bistate.Identifier2 = strdup(other.Context.Bistate.Identifier2);
            this->Context.Bistate.Emoji2 = strdup(other.Context.Bistate.Emoji2);
            this->Context.Bistate.LastIdentifier = strdup(other.Context.Bistate.LastIdentifier);
        }
        else
            memset(&this->Context, 0, sizeof(union Context));

        this->Emoji = strdup(other.Emoji);
        this->Creator = strdup(other.Creator);
        this->CreatedAt = other.CreatedAt;

        return *this;
    }

    cJSON *Device::JSON()
    {
        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "name", this->Name);
        cJSON_AddStringToObject(root, "type", this->Type);
        cJSON_AddStringToObject(root, "subtype", this->Subtype);
        cJSON_AddNumberToObject(root, "protocol", this->Protocol);

        cJSON *context = cJSON_AddObjectToObject(root, "context");
        if (!strcmp(this->Subtype, Subtypes::Button))
        {
            cJSON_AddStringToObject(context, "command", this->Context.Button.Command);
            cJSON_AddStringToObject(context, "emoji", this->Context.Button.Emoji);
        }
        else if (!strcmp(this->Subtype, Subtypes::Bistate))
        {
            cJSON_AddStringToObject(context, "identifier1", this->Context.Bistate.Identifier1);
            cJSON_AddStringToObject(context, "emoji1", this->Context.Bistate.Emoji1);
            cJSON_AddStringToObject(context, "identifier2", this->Context.Bistate.Identifier2);
            cJSON_AddStringToObject(context, "emoji2", this->Context.Bistate.Emoji2);
            cJSON_AddStringToObject(context, "last_identifier", this->Context.Bistate.LastIdentifier);
        }

        cJSON_AddStringToObject(root, "emoji", this->Emoji);
        cJSON_AddStringToObject(root, "creator", this->Creator);
        cJSON_AddNumberToObject(root, "created_at", this->CreatedAt);

        return root;
    }

    Controller *Controller::New(logger::Logger *logger, database::Database *database)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Controller();

        // Inject dependencies
        Instance->logger = logger;
        Instance->db = database->Open(DB_NAMESPACE);

        return Instance;
    }

    uint32_t Controller::Count()
    {
        uint32_t count;

        ESP_ERROR_CHECK(this->db->Count(&count));

        return count;
    }

    Device *Controller::Get(const char *name)
    {
        Device *device = NULL;

        cJSON *deviceJSON = NULL;
        ESP_ERROR_CHECK(this->db->Get(name, &deviceJSON));

        if (deviceJSON != NULL)
            device = new Device(deviceJSON);

        cJSON_Delete(deviceJSON);

        return device;
    }

    Device *Controller::List(uint32_t *size)
    {
        uint32_t count = this->Count();
        *size = count;
        if (count < 1)
            return NULL;

        Device *list = new Device[count];
        Device *aux = list;

        ESP_ERROR_CHECK(this->db->Find(
            [](const char *key, void *context) -> bool
            {
                Device *list = *(Device **)context;

                cJSON *deviceJSON = NULL;
                ESP_ERROR_CHECK(Instance->db->Get(key, &deviceJSON));

                *list = Device(deviceJSON);
                (*(Device **)context)++;

                cJSON_Delete(deviceJSON);
                return false;
            },
            &aux));

        return list;
    }

    void Controller::Set(Device *device)
    {
        cJSON *deviceJSON = device->JSON();
        ESP_ERROR_CHECK(this->db->Set(device->Name, deviceJSON));
        cJSON_Delete(deviceJSON);
    }

    void Controller::Delete(const char *name)
    {
        ESP_ERROR_CHECK(this->db->Delete(name));
    }

    void Controller::Drop()
    {
        ESP_ERROR_CHECK(this->db->Drop());
    }
}