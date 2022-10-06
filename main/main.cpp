#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "logger.hpp"

namespace main
{
    static const char *TAG = "main";

    class App
    {
    private:
        const esp_app_desc_t *description;
        logger::Logger logger;

    public:
        static App New()
        {
            App app;

            int64_t elapsed = esp_timer_get_time();

            app.description = esp_app_get_description();

            // Inject dependencies

            app.logger = logger::Logger::New((esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);

            elapsed = esp_timer_get_time() - elapsed;
            app.logger.Info(TAG, "Startup took %f ms", (float)(elapsed / 1000.0l));
            app.logger.Info(TAG, "==================== %s v%s ====================", app.description->project_name, app.description->version);

            return app;
        }
    };
}

// App dependencies container
static main::App app;

extern "C" void app_main(void)
{
    // Dependencies must be initialized after App main entrypoint
    app = main::App::New();
}
