idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       REQUIRES logger database provisioner chron device user esp_common esp_event
                                esp_wifi lwip esp_http_server http_parser json fatfs)
