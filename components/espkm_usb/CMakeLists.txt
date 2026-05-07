set(srcs)

if(CONFIG_ESPKM_USB_OTG_ENABLE)
    list(APPEND srcs
        "usb.c"
        "usb_desc.c"
    )
else()
    list(APPEND srcs
        "usb_stub.c"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES
        tinyusb
        espressif__esp_tinyusb
        espkm_core
        espkm_router
)
