/*
 * web_thing_property.c
 *
 *  Created on: Jun 27, 2019
 *      Author: kz
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "web_thing_property.h"
#include "common.h"

#define PROP_MODEL_LEN 500

void prop_value_str(property_t *p, char *b);
char *get_property_json(property_t *p);

//**********************************************************************
//initialize empty property structure
property_t *property_init(jsonize_t *vj, jsonize_t *mj){
	property_t * p;

	p = malloc(sizeof(property_t));
	memset(p, 0, sizeof(property_t));
	//set function for value jsonization, e.g. "speed":125
	if (vj != NULL){
		p -> value_jsonize = vj;
	}
	else{
		p -> value_jsonize = get_property_json;
	}
	//set function for model jsonization
	if (mj != NULL){
		p -> model_jsonize = mj;
	}
	else{
		p -> model_jsonize = NULL;
	}

	return p;
}

//***************************************************************************
char *get_properties_model(thing_t *t){
	char *prop, *buff_temp;
	property_t *p;

	int pq = t -> prop_quant;
	prop = malloc(pq * PROP_MODEL_LEN);
	memset(prop, 0, pq * PROP_MODEL_LEN);

	p = t -> properties;
	if (pq > 1){
		for (int i = pq; i > 0; i--){
			buff_temp = property_model_jsonize(p, t -> thing_nr);
			strcat(prop, buff_temp);
			free(buff_temp);
			if (i != 1){
				strcat(prop, ",");
			}
			p = p -> next;
		}
	}
	else{
		prop = property_model_jsonize(p, t -> thing_nr);
	}

	return prop;
}


// **************************************************************************
// create json model for value
char *property_model_jsonize(property_t *p, int16_t thing_index){
	char prop_str[] = "\"%s\":{\"@type\":\"%s\",\"title\":\"%s\","\
					"\"type\":\"%s\",%s\"description\":\"%s\",%s"\
					"\"readOnly\":%s,\"links\":"\
					"[{\"rel\":\"property\",\"href\":\"%sproperties/%s\"}]}";

	char num_str[] = "\"minimum\":%s,\"maximum\":%s,\"unit\":\"%s\",";

	char *type[] = {"null", "boolean", "object", "array",
					"number", "integer", "string"};
	char *bool_str[] = {"false", "true"};

	char *buff = NULL, *buff1, *buff_enum = NULL;
	char buff_min[15], buff_max[15], th_lk[10];
	bool build_json = false;

	if (p -> type == VAL_NULL){
		return NULL;
	}

	sprintf(th_lk, "/%i/", thing_index);

	//clear buffers
	buff1 = malloc(100);
	memset(buff1, 0, 100);
	memset(buff_min, 0, 15);
	memset(buff_max, 0, 15);

	if (p -> type == VAL_INTEGER){
		sprintf(buff_min, "%i", (int32_t)(p -> min_value.int_val));
		sprintf(buff_max, "%i", (int32_t)(p -> max_value.int_val));
		sprintf(buff1, num_str, buff_min, buff_max, p -> unit);
		build_json = true;
	}
	else if (p -> type == VAL_NUMBER){
		sprintf(buff_min, "%5.3f", p -> min_value.float_val);
		sprintf(buff_max, "%5.3f", p -> max_value.float_val);
		sprintf(buff1, num_str, buff_min, buff_max, p -> unit);
		build_json = true;
	}
	else if (p -> type == VAL_BOOLEAN){
		build_json = true;
	}
	else if (p -> type == VAL_OBJECT){
		if (p -> model_jsonize != NULL){
			buff1 = p -> model_jsonize(p);
			build_json = true;
		}
		else{
			printf("VAL_OBJECT: jsonization failed\n");
		}
	}
	else if (p -> type == VAL_STRING){
		if (p -> enum_prop == false){
			if (p -> model_jsonize != NULL){
				buff1 = p -> model_jsonize(p);
				build_json = true;
			}
			else{
				printf("object jsonization failed\n");
			}
		}
		else{
			//enum value
			buff_enum = malloc(200); //TODO: calculate needed place
			strcpy(buff_enum, "\"enum\":[");
			enum_item_t *enum_item;
			int enum_i = 0;

			enum_item  = p -> enum_list;
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
			strcat(buff_enum, "],");
			build_json = true;
		}
		//build_json = true;
	}
	else if (p -> type == VAL_ARRAY){
		if (p -> model_jsonize != NULL){
			buff1 = p -> model_jsonize(p);
			build_json = true;
		}
		else{
			printf("VAL_ARRAY: jsonization failed\n");
		}
	}

	if (build_json == true){
		buff = malloc(400);
		if (buff_enum != NULL){
			sprintf(buff, prop_str, p -> id, p -> at_type -> at_type, p -> title,
					type[p -> type], buff_enum, p -> description, buff1,
					bool_str[p -> read_only], th_lk, p -> id);
		}
		else{
			sprintf(buff, prop_str, p -> id, p -> at_type -> at_type, p -> title,
					type[p -> type], "", p -> description, buff1,
					bool_str[p -> read_only], th_lk, p -> id);
		}
	}
	
	free(buff_enum);
	free(buff1);
	
	return buff;
}


/************************************************************************
 *
 * prepare json representation of the property's value
 *
 * **********************************************************************/
char *get_property_json(property_t *p){
	char *buff;
	int32_t len;

	if (p -> type != VAL_STRING){
		len = strlen (p -> id) + 20;
	}
	else{
		len = strlen (p -> id) + 20;
	}
	buff = malloc(len);

	memset(buff, 0, len);
	buff[0] = '"';
	strcat(buff, p -> id);
	strcat(buff, "\":");
	prop_value_str(p, buff + strlen(buff));

	return buff;
}


/************************************************************************
 *
 * convert property value into string
 *
 ************************************************************************/
void prop_value_str(property_t *p, char *buff){

	if (xSemaphoreTake(p -> mux, 10) == pdTRUE ){
		char *buff1 = NULL;
		
		switch (p -> type){
		case VAL_NULL:
			break;
		case VAL_BOOLEAN:
			if (*(bool *)(p -> value) == true){
				strcpy(buff, "true");
			}
			else{
				strcpy(buff, "false");
			}
			break;
		case VAL_NUMBER:
			sprintf(buff, "%4.3f", *(double *)(p -> value));
			break;
		case VAL_INTEGER:
			sprintf(buff, "%i", *(int *)(p -> value));
			break;
		case VAL_STRING:
			strcat(buff, "\"");
			strcat(buff, (char *)p -> value);
			strcat(buff, "\"");
			break;
		case VAL_ARRAY:
		case VAL_OBJECT:
			buff1 = p -> value_jsonize(p);
			strcpy(buff, buff1);
			free(buff1);
			break;
		default:
			buff = NULL;
			printf("prop_value_str - not implemented");
		}
		xSemaphoreGive(p -> mux);
	}
}
