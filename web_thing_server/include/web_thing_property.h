/*
 * web_thing_property.h
 *
 *  Created on: Jun 27, 2019
 *      Author: kz
 */

#ifndef WEB_THING_PROPERTY_H_
#define WEB_THING_PROPERTY_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

//#include "simple_web_thing_server.h"
#include "web_thing.h"
#include "common.h"

#define PROP_VAL_LEN 50

typedef struct link_t link_t;
typedef struct enum_item_t enum_item_t;
/*
 * if new value is different then the old one set_callback_t must return "1" (one)
 */
typedef int16_t (set_callback_t)(char *new_value);

/*
 * function jsonize_t must return json representation of the property value in
 * format "name":value
 * e.g.: "temperature":20.45; "acceleration":[1,2,3]; "text":"This is text"
 */
typedef char *(jsonize_t)(property_t *p);

struct link_t{
	char *rel;
	char *href;
};

typedef union {
	int32_t int_val;
	float float_val;
} int_float_u;

struct property_t{
	char *id;
	at_type_t *at_type;
	char *title;
	VAL_TYPE type;
	char *description;
	void *value;
	int_float_u min_value;
	int_float_u max_value;
	int_float_u multiple_of;
	char *unit;
	bool enum_prop;
	enum_item_t *enum_list;
	bool read_only;
	link_t *links;
	link_t *last_link;
	property_t *next;
	property_t *last;
	set_callback_t *set;
	jsonize_t *value_jsonize;
	jsonize_t *model_jsonize;  //builds model for type OBJECT and ARRAY
	struct thing_t *t;
	xSemaphoreHandle mux;
};

union enum_value_t{
	int int_val;
	float float_val;
	char *str_addr;
};

struct enum_item_t{
	union enum_value_t value;
	bool current;
	enum_item_t *next;
};

property_t *property_init(jsonize_t *vj, jsonize_t *mj);
char *property_model_jsonize(property_t *t, int16_t thing_id);
char *get_properties_model(thing_t *t);

#endif /* WEB_THING_PROPERTY_H_ */
