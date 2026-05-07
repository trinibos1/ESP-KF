set(srcs)

if(CONFIG_ESPKM_CLI_ENABLE)
    list(APPEND srcs "cli.c")
else()
    list(APPEND srcs "cli_stub.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        console
        espkm_core
        espkm_router
        espkm_ble
)