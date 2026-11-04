file(GLOB_RECURSE SRCS *.cpp)
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS .
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server     esp_https_server esp_http_client esp-tls             app_update esp_app_format json mbedtls driver esp_driver_tsens esp_partition esp_hw_support esp_system spi_flash esp_timer lwip espressif__led_strip
)
