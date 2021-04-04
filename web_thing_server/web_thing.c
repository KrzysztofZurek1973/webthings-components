/*
 * web_thing.c
 *
 *  Created on: Jun 27, 2019
 *      Author: kz
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_thing.h"


//**********************************************************************
//initialize empty thing structure
thing_t *thing_init(void){
	thing_t *t;

	t = malloc(sizeof(thing_t));
	memset(t, 0, sizeof(thing_t));
	t -> thing_nr = -1;

	return t;
}

// ***************************************************************************
//add property to thing
int8_t add_property(thing_t *_t, property_t *_p){
	int res = 0;

	if (_t -> last_property == NULL){
		_t -> properties = _p;
	}
	else{
		_t -> last_property -> next = _p;
	}
	_t -> last_property = _p;

	_t -> prop_quant++;
	_p -> t = _t;

	return res;
}


// ***************************************************************************
//add action to thing
int8_t add_action(thing_t *_t, action_t *_a){
	int res = 0;

	action_t **a = &(_t -> actions);

	while (*a != NULL){
		a = &((*a) -> next);
	}

	*a = _a;
	_a -> t = _t;

	return res;
}


// ***************************************************************************
//add event to thing
int8_t add_event(thing_t *_t, event_t *_e){
	int res = 0;

	event_t **e = &(_t -> events);

	while (*e != NULL){
		e = &((*e) -> next);
	}

	*e = _e;
	_e -> t = _t;

	return res;
}


//**********************************************************************
//set or add @type to thing
int8_t set_thing_type(thing_t *t, at_type_t *at){
	int8_t res = 0;

	if (t -> at_type == NULL){
		t -> at_type = at;
		at -> next = NULL;
	}
	else{
		at_type_t *next = t -> at_type;

		while (next -> next != NULL){
			next = next -> next;
		}
		next -> next = at;
		at -> next = NULL;
	}

	t -> type_quantity++;

	return res;
}

// *************************************************************************
//create json model for single thing
int thing_jsonize(thing_t *t, char *host, char *domain, uint16_t port, char *buff){
	char thing_str[] = "\"name\":\"%s\",\"href\":\"%s\",\"@context\":\"%s\","\
					"\"@type\":[%s],\"properties\":{%s},\"actions\":{%s},"\
					"\"events\":{%s},\"links\":[%s],\"description\":\"%s\"";

	char links_str[] = "{\"rel\":\"properties\",\"href\":\"%s/properties\"},"\
					"{\"rel\":\"actions\",\"href\":\"%s/actions\"},"\
					"{\"rel\":\"events\",\"href\":\"%s/events\"},"\
					"{\"rel\":\"alternate\",\"href\":\"ws://%s.%s:%i%s\"}";

	char *links_buff, lk[6], lk_1[6], *props = NULL; 
	char *actions = NULL, *events = NULL, *at_types = NULL;
	int len = 0;

	sprintf(lk, "/%i", t -> thing_nr);
	memcpy(lk_1, lk, 5);

	//prepare links
	links_buff = malloc(300);
	//TODO: get IP address
	sprintf(links_buff, links_str, lk_1, lk_1, lk_1, host, domain, port, lk_1);

	//prepare thing's @types array
	int tq = t -> type_quantity;
	if (tq > 0){
		at_types = malloc(tq * 30);
		at_type_t *at = t -> at_type;
		if (tq > 1){
			for (int i = tq; i > 0; i--){
				strcat(at_types, "\"");
				strcat(at_types, at -> at_type);
				strcat(at_types, "\"");
				if (i != 1){
					strcat(at_types, ",");
				}
				at = at -> next;
			}
		}
		else{
			sprintf(at_types, "\"%s\"", at -> at_type);
		}
	}
	else{
		at_types = malloc(3);
		at_types[0] = '[';
		at_types[1] = ']';
		at_types[2] = 0;
	}

	//prepare properties
	props = get_properties_model(t);

	//prepare actions
	actions = get_actions_model(t);

	//prepare events
	events = get_events_model(t);

	//prepare thing model
	sprintf(buff, thing_str, t -> id, lk, t -> at_context,
			at_types, props, actions, events,
			links_buff, t -> description);
	len = strlen(buff);

	//release used memory
	free(events);
	free(actions);
	free(props);
	free(at_types);
	free(links_buff);

	return len;
}

//***********************************************************************
//get thing model
//e.g. GET /0
char *get_thing(thing_t *t, int16_t thing_index, char *host,
				char *domain, uint16_t port){
	char *buff = NULL; //*res_buff = NULL, 

	//find thing in the tree
	while (t != NULL){
		if (t -> thing_nr == thing_index){
			break;
		}
		t = t -> next;
	}

	//build the thing's model
	int model_len = t -> model_len;
	if (model_len == 0){
		model_len = 2000; //default value
	}
	buff = malloc(model_len);
	buff[0] = '{';
	buff[1] = 0;
	thing_jsonize(t, host, domain, port, buff + 1);
	strcat(buff, "}");

	return buff;
}

/*************************************************************
 *
 * add subscriber to the list of subscribers
 *
 * *************************************************************/
int8_t add_subscriber(connection_desc_t *_c){
	int8_t res = 0;
	subscriber_t *s;
	thing_t *_t = _c -> thing;

	s = malloc(sizeof(subscriber_t));
	s -> conn_desc = _c;
	s -> next = NULL;
	s -> prev = NULL;

	if (_t -> subscribers == NULL){
		_t -> subscribers = s;
	}
	else{
		_t -> last_subscriber -> next = s;
		s -> prev = _t -> last_subscriber;
	}
	_t -> last_subscriber = s;
	
	//printf("thing - subscriber added, %p\n", s);

	return res;
}

/*************************************************************
 *
 * delete subscriber from the list
 *
 * *************************************************************/
int8_t delete_subscriber(connection_desc_t *_c){
	int8_t res = 0;
	subscriber_t *s;
	thing_t *_t = _c -> thing;

	if ((_t != NULL) && (_c != NULL)){
		s = _t -> subscribers;
		while (s != NULL){
			if (s -> conn_desc == _c){
				break;
			}
			s = s -> next;
		}
		//delete s
		if (s != NULL){
			if (s -> next == NULL){
				//the last element
				if (s -> prev != NULL){
					s -> prev -> next = NULL;
					_t -> last_subscriber = s -> prev;
				}
				else{
					//only one element in the list
					_t -> last_subscriber = NULL;
					_t -> subscribers = NULL;
				}
			}
			else if (s -> prev == NULL){
				//the first element
				s -> next -> prev = NULL;
				_t -> subscribers = s -> next;
			}
			else{
				//element form the middle
				s -> prev -> next = s -> next;
				s -> next -> prev = s -> prev;
			}
			//printf("subscriber deleted, %p\n", s);
			free(s);
		}
		else{
			res = -1;
		}
	}
	else{
		res = -2;
	}

	return res;
}
