#include <string.h>
#include "esp_err.h"
#include "esp_random.h"
#include "cJSON.h"
#include "logger.hpp"
#include "role.hpp"
#include "database.hpp"
#include "user.hpp"

namespace user
{
    User::User()
    {
        this->Name = NULL;
        this->Password = NULL;
        this->Token = NULL;
        this->Role = NULL;
        this->Emoji = NULL;
        this->CreatedAt = 0;
    }

    User::User(const char *name, const char *password, const char *token,
               const char *role, const char *emoji, time_t createdAt)
    {
        this->Name = strdup(name);
        this->Password = strdup(password);
        this->Token = strdup(token);
        this->Role = strdup(role);
        this->Emoji = strdup(emoji);
        this->CreatedAt = createdAt;
    }

    User::User(cJSON *src)
    {
        this->Name = strdup(cJSON_GetObjectItem(src, "name")->valuestring);
        this->Password = strdup(cJSON_GetObjectItem(src, "password")->valuestring);
        this->Token = strdup(cJSON_GetObjectItem(src, "token")->valuestring);
        this->Role = strdup(cJSON_GetObjectItem(src, "role")->valuestring);
        this->Emoji = strdup(cJSON_GetObjectItem(src, "emoji")->valuestring);
        this->CreatedAt = cJSON_GetObjectItem(src, "created_at")->valueint;
    }

    User::~User()
    {
        free((void *)this->Name);
        free((void *)this->Password);
        free((void *)this->Token);
        free((void *)this->Role);
        free((void *)this->Emoji);
    }

    User &User::operator=(const User &other)
    {
        if (this == &other)
            return *this;

        free((void *)this->Name);
        free((void *)this->Password);
        free((void *)this->Token);
        free((void *)this->Role);
        free((void *)this->Emoji);

        this->Name = strdup(other.Name);
        this->Password = strdup(other.Password);
        this->Token = strdup(other.Token);
        this->Role = strdup(other.Role);
        this->Emoji = strdup(other.Emoji);
        this->CreatedAt = other.CreatedAt;

        return *this;
    }

    cJSON *User::JSON()
    {
        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "name", this->Name);
        cJSON_AddStringToObject(root, "password", this->Password);
        cJSON_AddStringToObject(root, "token", this->Token);
        cJSON_AddStringToObject(root, "role", this->Role);
        cJSON_AddStringToObject(root, "emoji", this->Emoji);
        cJSON_AddNumberToObject(root, "created_at", this->CreatedAt);

        return root;
    }

    bool User::Equals(const char *name) const
    {
        return !strcmp(this->Name, name);
    }

    bool User::Equals(User *other) const
    {
        return this->Equals(other->Name);
    }

    bool User::Equals(const User *other) const
    {
        return this->Equals(other->Name);
    }

    Controller *Controller::New(logger::Logger *logger, database::Database *database)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Controller();

        // Inject dependencies
        Instance->logger = logger;
        Instance->db = database->Open(DB_NAMESPACE);

        // Create or reset default system user
        Instance->Set((User *)(&System::System));

        return Instance;
    }

    uint32_t Controller::Count()
    {
        uint32_t count;

        ESP_ERROR_CHECK(this->db->Count(&count));

        return count;
    }

    User *Controller::Get(const char *name)
    {
        User *user = NULL;

        cJSON *userJSON = NULL;
        ESP_ERROR_CHECK(this->db->Get(name, &userJSON));

        if (userJSON != NULL)
            user = new User(userJSON);

        cJSON_Delete(userJSON);

        return user;
    }

    User *Controller::List(uint32_t *size)
    {
        uint32_t count = this->Count();
        *size = count;
        if (count < 1)
            return NULL;

        User *list = new User[count];
        User *aux = list;

        ESP_ERROR_CHECK(this->db->Find(
            [](const char *key, void *context) -> bool
            {
                User *list = *(User **)context;

                cJSON *userJSON = NULL;
                ESP_ERROR_CHECK(Instance->db->Get(key, &userJSON));

                *list = User(userJSON);
                (*(User **)context)++;

                cJSON_Delete(userJSON);
                return false;
            },
            &aux));

        return list;
    }

    void Controller::Set(User *user)
    {
        cJSON *userJSON = user->JSON();
        ESP_ERROR_CHECK(this->db->Set(user->Name, userJSON));
        cJSON_Delete(userJSON);
    }

    void Controller::Delete(const char *name)
    {
        ESP_ERROR_CHECK(this->db->Delete(name));
    }

    void Controller::Drop()
    {
        ESP_ERROR_CHECK(this->db->Drop());
    }

    void Controller::GenerateToken(char *token)
    {
        int size = strlen(TOKEN_CHARSET);

        for (int i = 0; i < TOKEN_SIZE; i++)
            token[i] = TOKEN_CHARSET[esp_random() % size];

        token[TOKEN_SIZE] = '\0';
    }

    bool Controller::Belongs(const char *user, const char *role)
    {
        User *userAux = this->Get(user);
        bool ret = this->Belongs(userAux, role);
        delete userAux;

        return ret;
    }

    bool Controller::Belongs(User *user, const char *role)
    {
        return !strcmp(user->Role, role);
    }

    bool Controller::Belongs(const char *user, role::Role *role)
    {
        return this->Belongs(user, role->Name);
    }

    bool Controller::Belongs(const char *user, const role::Role *role)
    {
        return this->Belongs(user, role->Name);
    }

    bool Controller::Belongs(User *user, role::Role *role)
    {
        return this->Belongs(user, role->Name);
    }

    bool Controller::Belongs(User *user, const role::Role *role)
    {
        return this->Belongs(user, role->Name);
    }
}