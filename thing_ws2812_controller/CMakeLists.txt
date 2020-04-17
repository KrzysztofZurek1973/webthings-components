idf_component_register(SRCS "thing_rgb_led_line.c" "ws2812b.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash
                       PRIV_REQUIRES web_thing_server rgb_patterns)


