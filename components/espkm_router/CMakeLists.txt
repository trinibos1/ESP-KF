idf_component_register(
  SRCS
    "router.c"
  INCLUDE_DIRS
    "include"
  REQUIRES
    freertos
    espkm_core
)

