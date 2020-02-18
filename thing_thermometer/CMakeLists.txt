idf_component_register(SRCS "thing_thermometer.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp32-owb esp32-ds18b20 web_thing_server)


