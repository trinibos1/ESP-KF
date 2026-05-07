idf_component_register(
  SRCS
    "keyboard.c"
    "keymaps/default/keymap.c"
  INCLUDE_DIRS
    "."
    "keymaps/default"
  REQUIRES
    espkm_core
    espkm_drivers
  WHOLE_ARCHIVE
)

