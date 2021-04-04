/*
 * web_thing_action.c
 *
 *  Created on: Nov 22, 2019
 *  Last update: Apr 2, 2021
 *      Author: kz
 *		 email: krzzurek@gmail.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "simple_web_thing_server.h"
#include "web_thing.h"
#include "web_thing_action.h"

char TRUE[] = "true";
char FALSE[] = "false";

char *action_model_jsonize(action_t *p, int16_t thing_index);
char *input_prop_jsonize(action_input_prop_t *aip);
char *request_inputs_jsonize(action_t *a, action_request_t *ar);
action_request_t *get_request_ptr(action_t *a, int request_index);


/******************************************************
 *
 * Complete action, set status as "completed" and time
 *
 * ****************************************************/
int8_t complete_action(int thing_nr, char *action_id, ACTION_STATUS status){
	int8_t res = -1;
	thing_t *t = NULL;
	action_t *a;
	int i;

	//find thing
	t = get_thing_ptr(thing_nr);
	if (t != NULL){
		a = get_action_ptr(t, action_id);

		if (a != NULL){
			//find request on the list
			i = a -> running_request_index;
			if (i > 0){
				action_request_t *ar = get_request_ptr(a, i);

				ar -> status = status;
				ar -> time_completed = time(NULL);
				a -> running_request_index = -1;
				res = 0;
				char *buff = action_request_jsonize(thing_nr, action_id, i);
				inform_all_subscribers_action(a, buff, strlen(buff));
				free(buff);
			}
		}
	}

	return res;
}

/**********************************************
 *
 * when action's status is changed upgrade it's record in actions list
 *
 * **********************************************/
void upgrade_request_on_list(int thing_nr, char *action_id, ACTION_STATUS status){

}


/*******************************************************
 *
 * jsonize action request from requests list
 *
 ******************************************************/
char *action_request_jsonize(int thing_nr, char *action_id, int request_index){
	thing_t *t = NULL;
	char *out_buff = NULL;
	action_t *a;
	char rq_str[] = "{\"%s\":{\"input\":{%s},\"href\":\"%s\",\"timeRequested\":\"%s\""\
					",%s\"status\":\"%s\"}}";
	//values in rq_str: action id, inputs ("name":value), href, request time,
	//complete time (with semicolon at the end, empty if not completed), status
	char *status[] = {"pending", "completed", "executed", "failed", "created", "deleted"};

	//find thing
	t = get_thing_ptr(thing_nr);
	a = get_action_ptr(t, action_id);

	//find action request
	if (a != NULL){
		action_request_t *ar = get_request_ptr(a, request_index);

		if (ar != NULL){
			char *inputs_buff, *href_buff = NULL;
			char *t_req = NULL, *t_com = NULL;
			struct tm ti;
			//jsonize inputs
			inputs_buff = request_inputs_jsonize(a, ar);

			//create href address
			href_buff = malloc(50);
			sprintf(href_buff, "/%i/actions/%s/%i", t ->thing_nr, action_id, request_index);

			//request time
			localtime_r(&(ar -> time_requested), &ti);
			t_req = malloc(50);
			memset(t_req, 0, 50);
			sprintf(t_req, "%04i-%02i-%02iT%02i:%02i:%02i+00:00", ti.tm_year + 1900,
					ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
			//complete time
			t_com = malloc(70);
			memset(t_com, 0, 70);
			if (ar -> time_completed > 0){
				localtime_r(&(ar -> time_completed), &ti);

				sprintf(t_com, "\"timeCompleted\":\"%04i-%02i-%02iT%02i:%02i:%02i+00:00\",",
							ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
							ti.tm_hour, ti.tm_min, ti.tm_sec);
			}
			else{
				strcpy(t_com, "");
			}

			int len = strlen(a -> id) + strlen(rq_str) + strlen(inputs_buff) +
					strlen(href_buff) +	strlen(t_req) + strlen(t_com) + 10;
			out_buff = malloc(len);

			sprintf(out_buff, rq_str, a -> id, inputs_buff, href_buff, t_req,
								t_com, status[ar -> status]);

			free(t_com);
			free(t_req);
			free(href_buff);
			free(inputs_buff);
		}
	}

	return out_buff;
}


/****************************************************
 *
 * jsonize inputs of given request
 *
 * **************************************************/
char *request_inputs_jsonize(action_t *a, action_request_t *ar){
	char prop_int_str[] = "\"%s\":%i";
	char prop_num_str[] = "\"%s\":%5.3f";
	char prop_str_str[] = "\"%s\":\"%s\"";
	int ipi;
	action_input_prop_t *ip;

	//prepare inputs list
	request_value_t *rv = ar -> values;
	//count how many values is saved in action_request
	int prop_qua = 0;
	while (rv != NULL){
		prop_qua++;
		rv = rv -> next;
	}
	//calculate how long the names of the inputs are 
	int id_len = 0;
	action_input_prop_t *ipt = a -> input_properties;
	while (ipt != NULL){
		id_len += strlen(ipt -> id);
		ipt = ipt -> next;
	}
	int inputs_len = prop_qua * 50 + id_len + 10;
	char *inputs = malloc(inputs_len);
	memset(inputs, 0, inputs_len);

	rv = ar -> values;
	while (rv != NULL){
		ipi = rv -> input_prop_index;
		//find input property name (id)
		ip = a -> input_properties;
		char *prop_id = NULL;
		while (ip != NULL){
			if (ip -> input_prop_index == ipi){
				prop_id = ip -> id;
				break;
			}
			ip = ip -> next;
		}
		if (prop_id != NULL){
			if (rv -> value != NULL){
				char *buff = malloc(strlen(ip -> id) + 30);
				memset(buff, 0, strlen(ip -> id) + 30);
				if (ip -> type == VAL_INTEGER){
					sprintf(buff, prop_int_str, ip -> id, *(int *)(rv -> value));
				}
				else if (ip -> type == VAL_NUMBER){
					sprintf(buff, prop_num_str, ip -> id, *(double *)(rv -> value));
				}
				else if (ip -> type == VAL_STRING){
					sprintf(buff, prop_str_str, ip -> id, (char *)(rv -> value));
				}
				else if (ip -> type == VAL_BOOLEAN){
					if (*(int8_t *)(rv -> value) == 0){
						sprintf(buff, prop_str_str, ip -> id, TRUE);
					}
					else{
						sprintf(buff, prop_str_str, ip -> id, FALSE);
					}
				}
				else{
					printf("INPUT JSONIZE: unknown type\n");
				}
				strcat(inputs, buff);
				free(buff);
			}
		}
		rv = rv -> next;
		if (rv != NULL){
			strcat(inputs, ",");
		}
	}

	return inputs;
}


/*************************************************
 *
 * while action is requested add it to the list of action's requests
 *
 * ************************************************/
int add_request_to_list(action_t *a, char *inputs){
	action_request_t *ar, *ar_last;
	time_t t;
	bool end_of_inputs = false;
	char *p_start, *p1, *p2, name[16], value[16];
	request_value_t *rv = NULL, *prev_rv = NULL, *first_rv = NULL;
	int next_index = -1;

	p_start = inputs;
	while (end_of_inputs == false){
		memset(name, 0, 15);
		memset(value, 0, 15);
		//name
		p1 = strchr(p_start, '"');
		if (p1 == NULL) goto add_request_to_list_end;
		p2 = strchr(p1 + 1, '"');
		if (p2 == NULL) goto add_request_to_list_end;
		int name_len = p2 - p1 - 1;
		memcpy(name, p1 + 1, name_len);
		//value
		p1 = strchr(p2, ':');
		if (p1 == NULL) goto add_request_to_list_end;
		p2 = strchr(p1 + 1, ',');
		if (p2 == NULL){
			p2 = inputs + strlen(inputs);
			end_of_inputs = true;
		}
		else{
			p_start = p2;
		}
		int val_len = p2 - p1 - 1;
		memcpy(value, p1 + 1, val_len);
		if (name_len > 0){
			action_input_prop_t *ap;
			//find input property of this name
			ap = a -> input_properties;
			while (ap != NULL){
				if (strcmp(name, ap -> id) == 0){
					break;
				}
				ap = ap -> next;
			}
			if (ap != NULL){
				rv = malloc(sizeof(request_value_t));
				if (prev_rv != NULL){
					prev_rv -> next = rv;
				}
				else{
					first_rv = rv;
				}
				rv -> input_prop_index = ap -> input_prop_index;
				rv -> next = NULL;
				if (val_len > 0){
					if (ap -> type == VAL_INTEGER){
						int *int_value;
						int_value = malloc(sizeof(int));
						*int_value = atoi(value);
						rv -> value = int_value;
					}
					else if (ap -> type == VAL_NUMBER){
						double *num_value;
						num_value = malloc(sizeof(double));
						*num_value = (double)atof(value);
						rv -> value = num_value;
					}
					else if (ap -> type == VAL_STRING){
						char *str_value = malloc(val_len);
						memcpy(str_value, value + 1, val_len - 2);
						str_value[val_len - 2] = 0;
						rv -> value = str_value;
					}
					else if (ap -> type == VAL_BOOLEAN){
						int8_t *bool_num_val;

						bool_num_val = malloc(sizeof(int8_t));
						if (strstr(value, TRUE) != NULL){
							*bool_num_val = 0;
						}
						else{
							*bool_num_val = 1;
						}
						rv -> value = bool_num_val;
					}
					else{
						//TODO
						printf("ADD REQUEST: Unknown input type\n");
					}
				}
				else{
					rv -> value = NULL;
				}
			}
		}
		prev_rv = rv;
	}

	t = time(NULL);
	ar = a -> requests_list;
	ar_last = a -> requests_list;
	int i = 0;
	while (ar != NULL){
		ar_last = ar;
		i++;
		ar = ar -> next;
	}
	//add request into requests list
	if (i == MAX_ACTION_REQUESTS){
		//request list is full, delete the oldest one to make place for new one
		action_request_t *ar_temp = a -> requests_list; //to be deleted
		a -> requests_list = a -> requests_list -> next;

		//delete values from the oldest request
		request_value_t *rv_temp = ar_temp -> values;
		request_value_t *rv_temp_prev = NULL;
		while (rv_temp != NULL){
			free(rv_temp -> value);
			rv_temp_prev = rv_temp -> next;
			free(rv_temp);
			rv_temp = rv_temp_prev;
		}
		//delete the oldest request
		free(ar_temp);
	}
	//add new request at the end of the list
	next_index = a -> last_request_index + 1;
	action_request_t *ar_new = malloc(sizeof(action_request_t));
	memset(ar_new, 0, sizeof(action_request_t));
	if (ar_last == NULL){
		a -> requests_list = ar_new;
	}
	else{
		ar_last -> next = ar_new;
	}
	ar_new -> time_requested = t;
	ar_new -> status = ACT_CREATED;
	ar_new -> time_completed = 0;
	if (first_rv != NULL){
		ar_new -> values = first_rv;
	}
	ar_new -> index = next_index;
	a -> last_request_index = next_index;
	a -> running_request_index = next_index;

add_request_to_list_end:
	return next_index;
}


/**************************************************
 *
 *
 *
 *************************************************/
char *get_actions_model(thing_t *t){
	char *act, *buff_temp;
	action_t *a;

	//count quantity of actions
	int i = 0;
	a = t -> actions;
	while (a != NULL){
		i++;
		a = a -> next;
	}
	//printf("actions: %i\n", i);
	if (i > 0){
		a = t -> actions;
		if (i > 1){
			act = malloc(i * 500);
			memset(act, 0, i * 500);
			for (int j = i; j > 0; j--){
				buff_temp = action_model_jsonize(a, t -> thing_nr);
				strcat(act, buff_temp);
				free(buff_temp);
				if (j != 1){
					strcat(act, ",");
				}
				a = a -> next;
			}
		}
		else{
			act = action_model_jsonize(a, t -> thing_nr);
		}
	}
	else{
		act = malloc(3);
		strcpy(act, "");
	}

	return act;
}


/********************************************************
 *
 * create json model for action
 *
 * *****************************************************/

char *action_model_jsonize(action_t *a, int16_t thing_index){
	char action_str[] = "\"%s\":{\"@type\":\"%s\",\"title\":\"%s\","\
					"\"description\":\"%s\","\
					"\"input\":{\"type\":\"object\",\"required\":[%s],"\
					"\"properties\":{%s}},\"links\":[{\"rel\":\"action\","\
					"\"href\":\"%sactions/%s\"}]}";

	char *buff = NULL, *all_prop_buff = NULL, *prop_buff, th_lk[10], *req_buff;
	uint16_t i = 0, ap_len = 0;
	action_input_prop_t *aip;

	sprintf(th_lk, "/%i/", thing_index);

	//jsonize input properties of the action
	aip = a -> input_properties;
	//count quantity of properties
	while (aip != NULL){
		i++;
		if (aip -> required == true){
			ap_len += strlen(aip -> id); //properties length of required prop.
		}
		aip = aip -> next;
	}

	uint16_t req_len = ap_len + i * 2 + 5;
	uint16_t buff_len = i * ACTION_PROP_LEN + strlen(a -> description) + req_len;
	all_prop_buff = malloc(buff_len);
	memset(all_prop_buff, 0, buff_len);

	req_buff = malloc(req_len);
	memset(req_buff, 0, req_len);

	aip = a -> input_properties;
	int req_cnt = 0;
	while (aip  != NULL){
		prop_buff = input_prop_jsonize(aip);
		strcat(all_prop_buff, prop_buff);
		free(prop_buff);
		if (aip -> required == true){
			if (req_cnt != 0){
				strcat(req_buff, ",");
			}
			strcat(req_buff, "\"");
			strcat(req_buff, aip -> id);
			strcat(req_buff, "\"");
			req_cnt++;
		}
		i--;
		if (i > 0){
			strcat(all_prop_buff, ",");
		}
		aip = aip -> next;
	}


	buff = malloc(400 + strlen(all_prop_buff));
	sprintf(buff, action_str, a -> id, a -> input_at_type -> at_type, a -> title, a -> description,
			req_buff, all_prop_buff, th_lk, a -> id);

	free(req_buff);
	free(all_prop_buff);

	return buff;
}


/***********************************************
 *
 * build json model of action input property
 *
 * *********************************************/
char *input_prop_jsonize(action_input_prop_t *aip){
	char *buff = NULL, *buff1 = NULL, *buff_enum = NULL;
	char *type[] = {"null", "boolean", "object", "array",
					"number", "integer", "string"};
	char prop_str[] = "\"%s\":{\"type\":\"%s\"";
	char buff_temp[30];
	bool add_comma = false;

	//allocate and clear buffers
	buff = malloc(ACTION_PROP_LEN);
	memset(buff, 0, ACTION_PROP_LEN);
	memset(buff_temp, 0, 30);

	if (aip -> type == VAL_INTEGER){
		if ((aip -> min_valid == true) || (aip -> max_valid == true) ||
				(aip -> unit != NULL)){
			buff1 = malloc(100);
			memset(buff1, 0, 100);
		}
		if (aip -> min_valid == true){
			sprintf(buff_temp, "\"minimum\":%i", aip -> min_value.int_val);
			strcat(buff1, buff_temp);
			add_comma = true;
		}
		if (aip -> max_valid == true){
			if (add_comma == true){
				strcat(buff1, ",");
			}
			sprintf(buff_temp, "\"maximum\":%i", aip -> max_value.int_val);
			strcat(buff1, buff_temp);
			add_comma = true;
		}
		if (aip -> unit != NULL){
			if (add_comma == true){
				strcat(buff1, ",");
			}
			sprintf(buff_temp, "\"unit\":\"%s\"", aip -> unit);
			strcat(buff1, buff_temp);
		}

		if ((aip -> enum_prop == true) && (aip -> enum_list != NULL)){
			//enum value
			char buff_loc_1[20];
			enum_item_t *enum_item;
			int enum_i = 0;

			//calculate needed place
			enum_item  = aip -> enum_list;
			while (enum_item){
				enum_i++;
				enum_item = enum_item -> next;
			}

			buff_enum = malloc(enum_i * 10); //TODO: calculate needed place
			strcpy(buff_enum, "\"enum\":[");

			enum_item  = aip -> enum_list;
			enum_i = 0;
			memset(buff_loc_1, 0, 20);
			while (enum_item){
				if (enum_i > 0){
					strcat(buff_enum, ",");
				}
				sprintf(buff_loc_1, "%d", enum_item -> value.int_val);
				strcat(buff_enum, buff_loc_1);
				memset(buff_loc_1, 0, 20);
				enum_i++;
				enum_item = enum_item -> next;
			}
			strcat(buff_enum, "]");
		}

	}
	else if (aip -> type == VAL_NUMBER){
		if ((aip -> min_valid == true) || (aip -> max_valid == true) ||
				(aip -> unit != NULL)){
			buff1 = malloc(100);
			memset(buff1, 0, 100);
		}
		//add maximum value
		if (aip -> min_valid == true){
			sprintf(buff_temp, "\"minimum\":%5.3f", aip -> min_value.float_val);
			strcat(buff1, buff_temp);
			add_comma = true;
		}
		//add minimum value
		if (aip -> max_valid == true){
			if (add_comma == true){
				strcat(buff1, ",");
			}
			sprintf(buff_temp, "\"maximum\":%5.3f", aip -> max_value.float_val);
			strcat(buff1, buff_temp);
			add_comma = true;
		}
		//add unit
		if (aip -> unit != NULL){
			if (add_comma == true){
				strcat(buff1, ",");
			}
			sprintf(buff_temp, "\"unit\":\"%s\"", aip -> unit);
			strcat(buff1, buff_temp);
		}

		if ((aip -> enum_prop == true) && (aip -> enum_list != NULL)){
			//enum value
			char buff_loc_1[20];
			enum_item_t *enum_item;
			int enum_i = 0;

			//calculate needed place
			enum_item  = aip -> enum_list;
			while (enum_item){
				enum_i++;
				enum_item = enum_item -> next;
			}

			buff_enum = malloc(enum_i * 10); //TODO: calculate needed place
			strcpy(buff_enum, "\"enum\":[");

			enum_item  = aip -> enum_list;
			enum_i = 0;
			memset(buff_loc_1, 0, 20);
			while (enum_item){
				if (enum_i > 0){
					strcat(buff_enum, ",");
				}
				sprintf(buff_loc_1, "%5.3f", enum_item -> value.float_val);
				strcat(buff_enum, buff_loc_1);
				memset(buff_loc_1, 0, 20);
				enum_i++;
				enum_item = enum_item -> next;
			}
			strcat(buff_enum, "]");
		}
	}
	else if (aip -> type == VAL_STRING){
		if ((aip -> enum_prop == true) && (aip -> enum_list != NULL)){
			enum_item_t *enum_item;
			int enum_i = 0;
			int len = 0;

			enum_item  = aip -> enum_list;
			//calculate needed place
			while (enum_item){
				len += strlen(enum_item -> value.str_addr);
				enum_i++;
				enum_item = enum_item -> next;
			}
			buff_enum = malloc(len + 20 + enum_i * 2);

			strcpy(buff_enum, "\"enum\":[");
			enum_item  = aip -> enum_list;
			enum_i = 0;
			while (enum_item){
				if (enum_i > 0){
					strcat(buff_enum, ",");
				}
				strcat(buff_enum, "\"");
				strcat(buff_enum, enum_item -> value.str_addr);
				strcat(buff_enum, "\"");
				enum_i++;
				enum_item = enum_item -> next;
			}
			strcat(buff_enum, "]");
		}
	}

	sprintf(buff, prop_str, aip -> id, type[aip -> type]);
	if (buff1 != NULL){
		strcat(buff, ",");
		strcat(buff, buff1);
		free(buff1);
	}
	if (buff_enum != NULL){
		strcat(buff, ",");
		strcat(buff, buff_enum);
		free(buff_enum);
	}
	strcat(buff, "}");

	return buff;
}


/******************************************
 *
 * initialization of action input
 *
 * ******************************************/
action_input_prop_t *action_input_prop_init(char *_id,
											VAL_TYPE _type,
											bool _req,		//required
											int_float_u *_min,
											int_float_u *_max,
											char *unit,
											bool is_enum,
											enum_item_t *enum_list){
	action_input_prop_t *aip;

	aip = malloc(sizeof(action_input_prop_t));
	aip -> id = _id;
	aip -> type = _type;
	aip -> required = _req;
	if (_min != NULL){
		aip -> min_value = *_min;
		aip -> min_valid = true;
	}
	else{
		aip -> min_valid = false;
	}
	if (_max != NULL){
		aip -> max_value = *_max;
		aip -> max_valid = true;
	}
	else{
		aip -> max_valid = false;
	}
	aip -> unit = unit;
	aip -> next = NULL;
	aip -> input_prop_index = -1;
	aip -> enum_prop = is_enum;
	aip -> enum_list = enum_list;

	return aip;
}


/*******************************************
 *
 *
 *
 * ******************************************/
void add_action_input_prop(action_t *_a, action_input_prop_t *_aip){
	int i;

	action_input_prop_t **aip = &(_a -> input_properties);

	while (*aip != NULL){
		i = (*aip) -> input_prop_index;
		aip = &((*aip) -> next);
	}

	*aip = _aip;
	i = _a -> inputs_qua;
	_aip -> input_prop_index = i + 1;
	_a -> inputs_qua = i + 1; //number of input properties
}


/******************************************************
 *
 * initialize action structure
 *
 * ****************************************************/
action_t *action_init(void){
	action_t *a;

	a = malloc(sizeof(action_t));
	memset(a, 0, sizeof(action_t));
	a -> last_request_index = 0;
	a -> running_request_index = -1;

	return a;
}


action_t *get_action_ptr(thing_t *t, char *action_id){
	action_t *a = NULL;

	if (action_id != NULL){
		//find action
		a = t -> actions;
		while (a != NULL){
			if (strcmp(action_id, a -> id) == 0){
				break;
			}
			a = a -> next;
		}
	}

	return a;
}


/*****************************************************
 *
 *
 *
 * ***************************************************/
action_request_t *get_request_ptr(action_t *a, int request_index){
	action_request_t *ar = a -> requests_list;

	while (ar != NULL){
		if (ar -> index == request_index){
			break;
		}
		ar = ar -> next;
	}
	return ar;
}


/*********************************************
 *
 * out: length of output string
 *
 * ********************************************/
uint16_t get_action_request_queue(action_t *a, char *buff){
	uint16_t res = 0;
	char *buff_1;
	action_request_t *ar = a -> requests_list;

	if (ar != NULL){
		while (ar != NULL){
			buff_1 = action_request_jsonize(a -> t -> thing_nr, a -> id, ar -> index);
			if (buff_1 != NULL){
				strcat(buff, buff_1);
				res += strlen(buff_1);
				free(buff_1);
			}
			ar = ar -> next;
			if (ar != NULL){
				strcat(buff, ",");
			}
		}
	}
	return res;
}
