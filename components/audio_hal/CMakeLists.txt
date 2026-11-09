idf_component_register(
    SRCS "audio_hal.c" "audio_controls.c" "audio_sampler.c"
    INCLUDE_DIRS "include"
    REQUIRES synth soundfont
    PRIV_REQUIRES board_esp32s31_coreboard bt_output esp_driver_gpio esp_driver_i2c
                  esp_driver_i2s esp_timer freertos midi midi_ingress log usb_midi_host
)

if(CONFIG_NEKTAR_P06_TELEMETRY)
    target_sources(${COMPONENT_LIB} PRIVATE "audio_perf.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NEKTAR_P06_TELEMETRY=1)
endif()
