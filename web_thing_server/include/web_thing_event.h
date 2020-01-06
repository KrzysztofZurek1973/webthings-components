/*
 * web_thing_event.h
 *
 *  Created on: Dec 16, 2019
 *      Author: Krzysztof Zurek
 *      e-mail: krzzurek@gmail.com
 */

#ifndef WEB_THING_EVENT_H_
#define WEB_THING_EVENT_H_

#include <time.h>

#include "common.h"
//#include "web_thing.h"

#define MAX_EVENTS 5

typedef struct event_t event_t;
typedef struct event_item_t event_item_t;
//typedef struct connection_desc_t connection_desc_t;
typedef struct event_subscriber_t event_subscriber_t;

struct event_subscriber_t{
	connection_desc_t *conn_desc;
	event_subscriber_t *prev;
	event_subscriber_t *next;
};

//thing event
struct event_t {
	char *id;
	char *title;
	char *description;
	VAL_TYPE type;
	char *at_type;
	char *unit;
	event_item_t *event_list;
	event_subscriber_t *subscribers;
	event_subscriber_t *last_subscriber;
	struct thing_t *t;
	event_t *next;
};

struct event_item_t{
	time_t timestamp;
	void *value;
	event_item_t *next;
};

event_t *
	event_init(void);
int8_t
	emit_event(int thing_nr, char *event_id, void *value);
char *
	get_events_model(thing_t *t);
char *
	event_list_jsonize(int thing_nr, char *event_id);
int8_t
	add_event_subscriber(event_t *_t, connection_desc_t *_c);
int8_t
	delete_event_subscriber(event_t *_t, connection_desc_t *_c);

#endif /* WEB_THING_EVENT_H_ */
