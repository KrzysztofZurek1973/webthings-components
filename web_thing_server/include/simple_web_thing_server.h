/*
 * websocket_server.h
 *
 *  Created on: May 6, 2019
 *      Author: kz
 */

#ifndef SIMPLE_WEB_THING_SERVER_H_
#define SIMPLE_WEB_THING_SERVER_H_

#include "freertos/FreeRTOS.h"
#include "lwip/api.h"
#include "freertos/timers.h"

#include "common.h"
#include "web_thing.h"
#include "web_thing_property.h"
#include "web_thing_event.h"
#include "web_thing_action.h"
#include "websocket.h"
#include "web_thing_mdns.h"

typedef struct {
	thing_t *things;
	thing_t *last_thing;
	uint16_t things_quantity;
	uint16_t port; //server port
	char host_name[20];
	char domain[10];
}root_node_t;

//configuration structure
typedef struct server_cfg{
	uint16_t port;
} server_cfg_t;

//server functions
int8_t start_web_thing_server(uint16_t port, char *host_name, char *domain);
int8_t root_node_init(void);
char *get_root_dir(void);

//thing functions
int8_t add_thing_to_server(thing_t *t);

char *get_resource_value(int8_t thing_id, RESOURCE_TYPE resource, char *name, int index);
int8_t set_resource_value(int8_t thing_id, char *name, char *new_value_str);
thing_t *get_thing_ptr(uint8_t thing_nr);
int8_t inform_all_subscribers_prop(property_t *_p);
int8_t inform_all_subscribers_action(action_t *_a, char *data, int len);
int8_t inform_all_subscribers_event(event_t *_e, char *data, int len);
int request_action(int8_t thing_nr, char *action_id, char *inputs);
int8_t close_thing_connection(connection_desc_t *conn_desc, char *tag);
//variables


#endif /* MAIN_WEBSOCKET_SERVER_H_ */
