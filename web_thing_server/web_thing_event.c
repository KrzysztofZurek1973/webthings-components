/*
 * web_thing_event.c
 *
 *  Created on: Jun 27, 2019
 *      Author: kz
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "simple_web_thing_server.h"
#include "web_thing_event.h"
#include "web_thing.h"

char *event_model_jsonize(event_t *a, int16_t thing_index);
event_t *get_event_ptr(thing_t *t, char *event_id);
int add_event_to_list(event_t *e, event_item_t *ei);
char *event_item_jsonize(event_t *e, event_item_t *ei);


/**/
int8_t add_event_subscriber(event_t *_t, connection_desc_t *_c){
	int8_t res = -1;

	return res;
}

/**/
int8_t delete_event_subscriber(event_t *_t, connection_desc_t *_c){
	int8_t res = -1;

	return res;
}


/****************************************************
 *
 * emit event
 *
 * **************************************************/
int8_t emit_event(int thing_nr, char *event_id, void *value){
	int8_t res = -1;
	thing_t *t = NULL;
	event_t *e = NULL;
	char *msg;

	//find thing
	t = get_thing_ptr(thing_nr);
	if (t != NULL){
		e = get_event_ptr(t, event_id);
		if (e != NULL){
			//add event to the event list
			event_item_t *ei;
			ei = malloc(sizeof(event_item_t));
			ei -> timestamp = time(NULL);
			ei -> next = NULL;
			ei -> value = value;

			add_event_to_list(e, ei);

			//send event to subscribers
			if (t -> subscribers != NULL){
				msg = event_item_jsonize(e, ei);
				inform_all_subscribers_event(e, msg, strlen(msg));
				free(msg);
			}
		}
	}

	return res;
}


/******************************************************
 *
 * jsonize one event emit item
 *
 * ***************************************************/
char *event_item_jsonize(event_t *e, event_item_t *ei){
	char *out_buff = NULL, *buff = NULL;
	void *value;
	char msg_str[] = "\"%s\":{\"data\":%s,\"timestamp\":\"%s\"}";

	value = ei -> value;

	//prepare event message
	switch(e -> type){
	case VAL_INTEGER:
		buff = malloc(10);
		memset(buff, 0, 10);
		sprintf(buff, "%i", *((int *)value));
		break;
	case VAL_NUMBER:
		buff = malloc(10);
		memset(buff, 0, 10);
		sprintf(buff, "%3.2f", *((double *)value));
		break;
	case VAL_STRING:
		buff = malloc(strlen(value) + 3);
		sprintf(buff, "\"%s\"", (char *)value);
		break;
	default:
		buff = malloc(3);
		strcpy(buff, "");
	}
	//request time
	struct tm ti;

	localtime_r(&(ei -> timestamp), &ti);
	char *t_event = malloc(50);
	memset(t_event, 0, 50);
	sprintf(t_event, "%04i-%02i-%02iT%02i:%02i:%02i+00:00", ti.tm_year + 1900,
			ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);

	out_buff = malloc(strlen(msg_str) + strlen(t_event) + strlen(buff)
			+ strlen(e -> id) + 1);
	sprintf(out_buff, msg_str, e -> id, buff, t_event);
	free(t_event);
	free(buff);

	return out_buff;
}


/***************************************************
 *
 * initialize event structure
 *
 * *************************************************/
event_t *event_init(void){
	event_t *e;

	e = malloc(sizeof(event_t));
	memset(e, 0, sizeof(event_t));

	return e;
}


//***************************************************************************
char *get_events_model(thing_t *t){
	char *events, *buff_temp;
	event_t *e;

	//count quantity of events
	int i = 0;
	e = t -> events;
	while (e != NULL){
		i++;
		e = e -> next;
	}
	//printf("events: %i\n", i);
	if (i > 0){
		events = malloc(i * 500);
		memset(events, 0, i * 500);

		e = t -> events;
		if (i > 1){
			for (int j = i; j > 0; j--){
				buff_temp = event_model_jsonize(e, t -> thing_nr);
				strcat(events, buff_temp);
				free(buff_temp);
				if (j != 1){
					strcat(events, ",");
				}
				e = e -> next;
			}
		}
		else{
			events = event_model_jsonize(e, t -> thing_nr);
		}
	}
	else{
		events = malloc(3);
		strcpy(events, "");
	}

	return events;
}


/********************************************************
 *
 * create json model for event
 *
 * *****************************************************/

char *event_model_jsonize(event_t *e, int16_t thing_index){
	char event_str[] = "\"%s\":{\"title\":\"%s\","\
					"\"description\":\"%s\",\"@type\":\"%s\","\
					"\"type\":\"%s\",\"unit\":\"%s\","\
					"\"links\":[{\"rel\":\"event\","\
					"\"href\":\"%sevents/%s\"}]}";
	char *type[] = {"null", "boolean", "object", "array",
					"number", "integer", "string"};

	char *buff = NULL, th_lk[10];

	sprintf(th_lk, "/%i/", thing_index);

	//uint16_t req_len = ap_len + i * 2 + 5;
	uint16_t model_len = strlen(event_str) + strlen(e -> description) +
						2 * strlen(e -> id) + 40;

	buff = malloc(model_len);
	sprintf(buff, event_str, e -> id, e -> title, e -> description,
			e -> at_type, type[e -> type], e -> unit, th_lk, e -> id);

	return buff;
}


/**************************************************************
 *
 *
 *
 * ************************************************************/
int add_event_to_list(event_t *e, event_item_t *ei){
	int res = -1;

	//count events in the list
	event_item_t *event_item = e -> event_list;
	event_item_t *last_event_item = e -> event_list;
	int i = 0;
	while (event_item != NULL){
		last_event_item = event_item;
		event_item = event_item -> next;
		i++;
	}

	//add event into list
	if (i == MAX_EVENTS){
		//list is full, delete the oldest item to make place for new one
		event_item_t *ei_temp = e -> event_list; //to be deleted
		e -> event_list = e -> event_list -> next;

		//delete value from the oldest item
		free(ei_temp -> value);
		free(ei_temp);
	}

	if (last_event_item == NULL){
		e -> event_list = ei;
	}
	else{
		last_event_item -> next = ei;
	}

	return res;
}


/**/
event_t *get_event_ptr(thing_t *t, char *event_id){
	event_t *e = NULL;

	if (event_id != NULL){
		//find action
		e = t -> events;
		while (e != NULL){
			if (strcmp(event_id, e -> id) == 0){
				break;
			}
			e = e -> next;
		}
	}

	return e;
}


/**********************************************************
 *
 *
 *
 * ********************************************************/
char *event_list_jsonize(int thing_nr, char *event_id){
	char *buff = NULL;
	thing_t *t;
	int i = 0;

	t = get_thing_ptr(thing_nr);

	if (event_id == NULL){
		//all events for this thing
		//count the number of all events
		event_t *e = t -> events;
		while (e != NULL){
			event_item_t *ei = e -> event_list;
			while (ei != NULL){
				i++;
				ei = ei -> next;
			}
			e = e -> next;
		}

		if (i > 0){
			buff = malloc(i * 100);
			memset(buff, 0, i * 100);
			strcat(buff, "[");
			e = t -> events;
			while (e != NULL){
				event_item_t *ei = e -> event_list;
				while (ei != NULL){
					char *temp_buff = event_item_jsonize(e, ei);
					strcat(buff, "{");
					strcat(buff, temp_buff);
					strcat(buff, "}");
					free(temp_buff);
					ei = ei -> next;
					if (ei != NULL){
						strcat(buff, ",");
					}
				}
				e = e -> next;
				if (e != NULL){
					if (e -> event_list != NULL){
						strcat(buff, ",");
					}
				}
			}
			strcat(buff, "]");
		}
		else{
			//no events emitted yet
			buff = malloc(3);
			buff[0] = '[';
			buff[1] = ']';
			buff[2] = 0;
		}
	}
	else{
		//only particular event
		event_t *e = get_event_ptr(t, event_id);
		if (e != NULL){
			event_item_t *ei = e -> event_list;
			while (ei != NULL){
				i++;
				ei = ei -> next;
			}
			if (i > 0){
				buff = malloc(i * 100);
				memset(buff, 0, i * 100);
				strcat(buff, "[");

				event_item_t *ei = e -> event_list;
				while (ei != NULL){
					char *temp_buff = event_item_jsonize(e, ei);
					strcat(buff, "{");
					strcat(buff, temp_buff);
					strcat(buff, "}");
					free(temp_buff);
					ei = ei -> next;
					if (ei != NULL){
						strcat(buff, ",");
					}
				}
				strcat(buff, "]");
			}
			else{
				//no events emitted yet
				buff = malloc(3);
				buff[0] = '[';
				buff[1] = ']';
				buff[2] = 0;
			}
		}
	}

	return buff;
}
