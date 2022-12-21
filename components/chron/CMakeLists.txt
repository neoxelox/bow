idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       REQUIRES logger i2cdev ds3231 freertos esp_common esp_event esp_timer esp_wifi lwip)
