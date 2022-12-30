#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "logger.hpp"
#include "gpio.hpp"
#include "status.hpp"

namespace status
{
    Controller *Controller::New(logger::Logger *logger)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Controller();

        // Inject dependencies
        Instance->logger = logger;

        // Initialize RGB LED
        Instance->redPin = gpio::Analog::New(GPIO_NUM_14, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        Instance->greenPin = gpio::Analog::New(GPIO_NUM_13, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        Instance->bluePin = gpio::Analog::New(GPIO_NUM_12, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        Instance->redPin->SetLevel(0);
        Instance->greenPin->SetLevel(0);
        Instance->bluePin->SetLevel(0);

        // Initialize RGB LED queue
        Instance->queue = xQueueCreate(QUEUE_SIZE, sizeof(Status));
        if (!Instance->queue)
            ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

        // Create RGB LED task
        xTaskCreatePinnedToCore(Instance->taskFunc, "Status", 4 * 1024, NULL, 6, &Instance->taskHandle, tskNO_AFFINITY);

        return Instance;
    }

    void Controller::setColor(Color color, uint8_t brightness)
    {
        this->redPin->SetLevel((color.Red * brightness) / 100);
        this->greenPin->SetLevel((color.Green * brightness) / 100);
        this->bluePin->SetLevel((color.Blue * brightness) / 100);
    }

    esp_err_t Controller::SetStatus(Status status)
    {
        if (!status.Duration && !status.Loop)
            return ESP_ERR_INVALID_ARG;

        status.Brightness = status.Brightness <= 100 ? status.Brightness : 100;

        if (xQueueSend(this->queue, &status, 0) == errQUEUE_FULL)
            return ESP_ERR_NO_MEM;

        return ESP_OK;
    }

    void Controller::taskFunc(void *args)
    {
        Status backStatus = Statuses::Off;
        Status frontStatus = Statuses::Off;
        bool toggle = false;

        while (1)
        {
            if (xQueueReceive(Instance->queue, &frontStatus,
                              (frontStatus.Duration >= MIN_TASK_WAIT ? frontStatus.Duration : MIN_TASK_WAIT) / portTICK_PERIOD_MS))
            {
                toggle = false;
                if (frontStatus.Loop)
                    backStatus = frontStatus;
            }

            if ((toggle = !toggle) || frontStatus.Duration == NULL)
                Instance->setColor(frontStatus.Color, frontStatus.Brightness);
            else if (frontStatus.Loop)
                Instance->setColor(Statuses::Off.Color, Statuses::Off.Brightness);
            else
            {
                Instance->setColor(backStatus.Color, backStatus.Brightness);
                toggle = true;
                frontStatus = backStatus;
            }
        }
    }
}