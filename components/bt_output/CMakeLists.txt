if(CONFIG_NEKTAR_P06B_BLUETOOTH)
    set(sources "bt_output.c" "bt_pcm.c" "bt_route.c")
endif()
idf_component_register(SRCS ${sources} INCLUDE_DIRS "include" PRIV_REQUIRES bt esp_timer freertos)

if(CONFIG_NEKTAR_P06B_BLUETOOTH)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=config_new" "-Wl,--wrap=config_save")
endif()
