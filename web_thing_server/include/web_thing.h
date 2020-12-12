/*
 * web_thing.h
 *
 *  Created on: Jun 27, 2019
 *      Author: kz
 */

#ifndef WEB_THING_H_
#define WEB_THING_H_

#include "web_thing_property.h"
#include "web_thing_event.h"
#include "web_thing_action.h"
#include "common.h"

#define THING_MODEL_LEN 3000
#define things_context "https://webthings.io/schemas"

typedef struct subscriber_t subscriber_t;

struct subscriber_t{
	connection_desc_t *conn_desc;
	subscriber_t *prev;
	subscriber_t *next;
};

struct thing_t{
	int8_t thing_nr;
	char *id;
	char *at_context;
	at_type_t *at_type;
	int8_t type_quantity;
	char *description;
	char *links;			//fill during server start
	property_t *properties;
	action_t *actions;
	event_t *events;
	uint8_t prop_quant;		//properties quantity
	property_t *last_property;
	uint16_t model_len;		//length of json model
	subscriber_t *subscribers;
	subscriber_t *last_subscriber;
	thing_t *next;
};

thing_t	*
	thing_init(void);
int8_t
	set_thing_type(thing_t *t, at_type_t *at);
int
	thing_jsonize(thing_t *t, char *host, char * domain, uint16_t port, char *buff);
int8_t
	add_property(thing_t *t, property_t *p);
int8_t
	add_action(thing_t *_t, action_t *_a);
int8_t
	add_event(thing_t *t, event_t *a);
char *
	get_thing(thing_t *t, int16_t thing_index, char *host, char *domain, uint16_t port);
int8_t
	add_subscriber(connection_desc_t *_c);
int8_t
	delete_subscriber(connection_desc_t *_c);

#endif /* WEB_THING_H_ */
