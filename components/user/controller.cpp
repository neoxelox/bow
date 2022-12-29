#include <string.h>
#include "esp_err.h"
#include "esp_random.h"
#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"
#include "user.hpp"

namespace user
{
    User::User(const char *Name, const char *Password, const char *Token,
               const char *Role, const char *Emoji, time_t CreatedAt)
    {
        this->Name = strdup(Name);
        this->Password = strdup(Password);
        this->Token = strdup(Token);
        this->Role = strdup(Role);
        this->Emoji = strdup(Emoji);
        this->CreatedAt = CreatedAt;
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

        ESP_ERROR_CHECK(db->Count(&count));

        return count;
    }

    User *Controller::Get(const char *name)
    {
        User *user = NULL;

        cJSON *userJSON = NULL;
        ESP_ERROR_CHECK(Instance->db->Get(name, &userJSON));

        if (userJSON != NULL)
            user = new User(userJSON);

        cJSON_Delete(userJSON);

        return user;
    }

    void Controller::Set(User *user)
    {
        cJSON *userJSON = user->JSON();
        ESP_ERROR_CHECK(db->Set(user->Name, userJSON));
        cJSON_Delete(userJSON);
    }

    void Controller::Delete(const char *name)
    {
        ESP_ERROR_CHECK(db->Delete(name));
    }

    void Controller::Drop()
    {
        ESP_ERROR_CHECK(db->Drop());
    }

    void Controller::GenerateToken(char *token)
    {
        int size = strlen(TOKEN_CHARSET);

        for (int i = 0; i < TOKEN_SIZE; i++)
            token[i] = TOKEN_CHARSET[esp_random() % size];

        token[TOKEN_SIZE] = '\0';
    }
}