idf_component_register(
    SRCS "midi_parser.c" "midi_queue.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
)
