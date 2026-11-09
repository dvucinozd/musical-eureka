idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "."
    PRIV_REQUIRES audio_hal esp_driver_uart esp_system esp_hw_support esp_psram heap log
                  midi_ingress spi_flash usb_midi_host)

