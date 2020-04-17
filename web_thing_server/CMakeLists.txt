set(srcs
	"simple_web_thing_server.c"
	"http_parser.c"
	"websocket.c"
	"web_thing.c"
	"web_thing_action.c"
	"web_thing_event.c"
	"web_thing_property.c"
	"web_thing_mdns.c"
	"web_thing_softap.c"
	"reset_button.c")
	
set(requires mbedtls mdns nvs_flash)
set(include_dirs "include")


idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       PRIV_REQUIRES "${requires}")
