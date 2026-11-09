idf_component_register(
    SRCS "synth.c" "synth_looper.c" "synth_sound.c" "synth_web_keys.c"
    INCLUDE_DIRS "include"
    REQUIRES midi
)
