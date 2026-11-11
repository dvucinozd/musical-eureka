idf_component_register(SRCS "app_main.c" "app_wifi.c" "app_web.c" "app_storage.c" "preset_codec.c" INCLUDE_DIRS "."
    EMBED_TXTFILES "index.html"
    PRIV_REQUIRES audio_hal bt_output esp_driver_uart esp_system esp_hw_support esp_psram heap log
                  midi_ingress spi_flash usb_midi_host esp_wifi esp_netif esp_event esp_http_server
                  nvs_flash bank_store soundfont esp_partition mbedtls esp_timer app_update)

if(CONFIG_NEKTAR_P07_OTA)
    target_sources(${COMPONENT_LIB} PRIVATE "app_ota.c" "app_ota_boot.c" "ota_image.c" "ota_transaction.c")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u nektar_ota_profile")
endif()

