idf_component_register(
  SRCS "src/bidi_switch_knob.c"
  INCLUDE_DIRS "./src"
  REQUIRES esp_driver_gpio esp_timer)
