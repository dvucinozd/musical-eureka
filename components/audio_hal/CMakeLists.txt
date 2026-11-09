idf_component_register(
    SRCS "audio_hal.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board_esp32s31_coreboard esp_driver_gpio esp_driver_i2c
                  esp_driver_i2s esp_timer freertos midi midi_ingress synth log usb_midi_host
)
