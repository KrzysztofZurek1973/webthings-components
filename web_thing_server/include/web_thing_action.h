/*
 * web_thing_action.h
 *
 *  Created on: Jun 27, 2019
 *	Last update: Apr 02, 2021
 *      Author: kz
 */

#ifndef WEB_THING_ACTION_H_
#define WEB_THING_ACTION_H_

#include <time.h>

#include "common.h"
#include "web_thing.h"

typedef struct action_t action_t;
typedef struct action_input_prop_t action_input_prop_t;
typedef struct action_request_t action_request_t;
typedef struct request_value_t request_value_t;
typedef int16_t (action_run_callback_t)(char *new_value);
typedef char *(jsonize_t)(property_t *p);

//thing action
struct action_t {
	char *id;
	char *title;
	char *description;
	at_type_t *input_at_type;
	int inputs_qua;
	action_input_prop_t *input_properties;
	action_request_t *requests_list;
	action_run_callback_t *run;
	struct thing_t *t;
	int running_request_index;
	int last_request_index;
	action_t *next;
};

//action input definition in action model
struct action_input_prop_t{
	char *id;
	VAL_TYPE type;
	bool required;
	int_float_u min_value;
	bool min_valid;
	int_float_u max_value;
	bool max_valid;
	char *unit;
	int input_prop_index;
	bool enum_prop;
	enum_item_t *enum_list;
	action_input_prop_t *next;
};

//action requested
struct action_request_t{
	int index;
	time_t time_requested;
	time_t time_completed;
	ACTION_STATUS status;
	request_value_t *values;
	action_request_t *next;
};

//values set by client during action request
struct request_value_t{
	int input_prop_index;
	void *value;
	request_value_t *next;
};

action_t *
	action_init(void);
action_input_prop_t *action_input_prop_init(char *,
											VAL_TYPE,
											bool,			//required
											int_float_u*,	//min
											int_float_u*,	//max
											char*,			//unit
											bool,			//enum
											enum_item_t*);	//enum list
action_t *
	get_action_ptr(thing_t *t, char *action_id);
void
	add_action_input_prop(action_t *, action_input_prop_t *);
char *
	get_actions_model(thing_t *t);
int
	add_request_to_list(action_t *a, char *inputs);
void
	upgrade_request_on_list(int thing_nr, char *action_id, ACTION_STATUS status);
char *
	action_request_jsonize(int thing_nr, char *action_id, int action_index);
int8_t
	complete_action(int thing_nr, char *action_id, ACTION_STATUS status);
uint16_t
	get_action_request_queue(action_t *a, char *buff);

#endif /* WEB_THING_ACTION_H_ */
