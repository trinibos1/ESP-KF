idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                      espkm_core
                      espkm_drivers
                      espkm_router
                      espkm_usb
                      espkm_ble
                      espkm_macros
                      demo_macropad
                      espkm_cli)
