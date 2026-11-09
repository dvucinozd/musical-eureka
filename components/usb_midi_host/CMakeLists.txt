idf_component_register(
    SRCS "usb_midi_host.c" "usb_midi_desc.c"
    INCLUDE_DIRS "include"
    REQUIRES usb
    PRIV_REQUIRES log esp_timer freertos
)
