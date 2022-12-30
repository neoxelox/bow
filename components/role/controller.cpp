#include <string.h>
#include "esp_err.h"
#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"
#include "role.hpp"

namespace role
{
    Role::Role()
    {
        this->Name = NULL;
        this->Devices = NULL;
        this->Emoji = NULL;
        this->Creator = NULL;
        this->CreatedAt = 0;
    }

    Role::Role(const char *name, const char **devices, const char *emoji,
               const char *creator, time_t createdAt)
    {
        this->Name = strdup(name);

        int size = 0;
        while (devices[size] != NULL)
            size++;
        this->Devices = (const char **)malloc((size + 1) * sizeof(char *));
        for (int i = 0; i < size; i++)
            this->Devices[i] = strdup(devices[i]);
        this->Devices[size] = NULL;

        this->Emoji = strdup(emoji);
        this->Creator = strdup(creator);
        this->CreatedAt = createdAt;
    }

    Role::Role(cJSON *src)
    {
        this->Name = strdup(cJSON_GetObjectItem(src, "name")->valuestring);

        cJSON *devices = cJSON_GetObjectItem(src, "devices");
        int size = cJSON_GetArraySize(devices);
        this->Devices = (const char **)malloc((size + 1) * sizeof(char *));
        for (int i = 0; i < size; i++)
            this->Devices[i] = strdup(cJSON_GetArrayItem(devices, i)->valuestring);
        this->Devices[size] = NULL;

        this->Emoji = strdup(cJSON_GetObjectItem(src, "emoji")->valuestring);
        this->Creator = strdup(cJSON_GetObjectItem(src, "creator")->valuestring);
        this->CreatedAt = cJSON_GetObjectItem(src, "created_at")->valueint;
    }

    Role::~Role()
    {
        free((void *)this->Name);

        for (int i = 0; this->Devices[i] != NULL; i++)
            free((void *)this->Devices[i]);
        free((void *)this->Devices);

        free((void *)this->Emoji);
        free((void *)this->Creator);
    }

    Role &Role::operator=(const Role &other)
    {
        if (this == &other)
            return *this;

        free((void *)this->Name);

        for (int i = 0; this->Devices[i] != NULL; i++)
            free((void *)this->Devices[i]);
        free((void *)this->Devices);

        free((void *)this->Emoji);
        free((void *)this->Creator);

        this->Name = strdup(other.Name);

        int size = 0;
        while (other.Devices[size] != NULL)
            size++;
        this->Devices = (const char **)malloc((size + 1) * sizeof(char *));
        for (int i = 0; i < size; i++)
            this->Devices[i] = strdup(other.Devices[i]);
        this->Devices[size] = NULL;

        this->Emoji = strdup(other.Emoji);
        this->Creator = strdup(other.Creator);
        this->CreatedAt = other.CreatedAt;

        return *this;
    }

    cJSON *Role::JSON()
    {
        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "name", this->Name);

        int size = 0;
        while (this->Devices[size] != NULL)
            size++;
        cJSON *devices = cJSON_AddArrayToObject(root, "devices");
        for (int i = 0; i < size; i++)
            cJSON_AddItemToArray(devices, cJSON_CreateString(this->Devices[i]));

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

    Role *Controller::Get(const char *name)
    {
        Role *role = NULL;

        cJSON *roleJSON = NULL;
        ESP_ERROR_CHECK(this->db->Get(name, &roleJSON));

        if (roleJSON != NULL)
            role = new Role(roleJSON);

        cJSON_Delete(roleJSON);

        return role;
    }

    Role *Controller::List(uint32_t *size)
    {
        uint32_t count = this->Count();
        *size = count;
        if (count < 1)
            return NULL;

        Role *list = new Role[count];
        Role *aux = list;

        ESP_ERROR_CHECK(this->db->Find(
            [](const char *key, void *context) -> bool
            {
                Role *list = *(Role **)context;

                cJSON *roleJSON = NULL;
                ESP_ERROR_CHECK(Instance->db->Get(key, &roleJSON));

                *list = Role(roleJSON);
                (*(Role **)context)++;

                cJSON_Delete(roleJSON);
                return false;
            },
            &aux));

        return list;
    }

    void Controller::Set(Role *role)
    {
        cJSON *roleJSON = role->JSON();
        ESP_ERROR_CHECK(this->db->Set(role->Name, roleJSON));
        cJSON_Delete(roleJSON);
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