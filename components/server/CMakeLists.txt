idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       REQUIRES logger database provisioner chron user device trigger role freertos
                                esp_common esp_event esp_wifi lwip esp_http_server http_parser json
                                fatfs esp_hw_support esp_app_format esp_system spi_flash esp_psram)
