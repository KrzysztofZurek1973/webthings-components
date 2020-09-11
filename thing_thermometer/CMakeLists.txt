set(srcs "thing_thermometer.c")	
set(include_dirs "./include")
set(requires nvs_flash)
set(priv_requires esp32-owb esp32-ds18b20 web_thing_server esp32_ota_updater)

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES "${requires}"
                       PRIV_REQUIRES "${priv_requires}")
