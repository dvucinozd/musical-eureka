idf_component_register(
    SRCS "midi_ingress.c"
    INCLUDE_DIRS "include"
    REQUIRES midi synth
    PRIV_REQUIRES usb_midi_host esp_timer freertos
)
