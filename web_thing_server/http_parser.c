/*
 * html_parser.c
 *  This file is a part of the "Simple Web Thing Server" project
 *  Created on: June 18, 2019
 *  Last edit:	Feb 19, 2021
 *      Author: Krzysztof Zurek
 *		e-mail: krzzurek@gmail.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "http_parser.h"
#include "simple_web_thing_server.h"

extern root_node_t root_node;

int16_t get_parser(char *rq, char **res, uint16_t things, uint16_t len);
int16_t put_parser(char *rq, char **res, uint16_t things, uint16_t len);
int16_t post_parser(char *rq, char **res, uint16_t things, uint16_t len);

char http_head[] = "HTTP/1.1 ";
char http_status_200[] = "200 OK\r\n";
char http_status_201[] = "201 Created\r\n";
char http_status_204[] = "204 No Content\r\n";
char http_status_500[] = "500 Internal Server Error\r\n";
char http_status_400[] = "400 Bad Request\r\n";

char keep_alive_resp[] = "Connection: Keep-Alive\r\n";
char keep_alive_resp_param[] = "Keep-Alive: timeout=2, max=100\r\n";

char h1_200[] = "Access-Control-Allow-Origin: *\r\n"\
				"Content-Type: application/td+json; charset=utf-8\r\n\r\n";

char h1_201[] = "Access-Control-Allow-Origin: *\r\n"\
				"Content-Type: application/json; charset=utf-8\r\n\r\n";

char h1_204[] = "Access-Control-Allow-Origin: *\r\n"\
				"Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"\
				"Access-Control-Allow-Headers: content-type\r\n"\
				"Access-Control-Max-Age: 86400\r\n\r\n";

char h1_500[] = "Access-Control-Allow-Origin: *\r\n"\
				"Content-Type: text/html; charset=utf-8\r\n\r\n";
				

//*********************************
char *prepare_http_header(int16_t status, bool keep_alive);
//parse html request
int16_t parse_http_request(char *rq, 
							char **res, 
							uint16_t tcp_len, 
							connection_desc_t *conn_desc);


/**********************************************************************
 *
 * receive and process http request
 *
 * ********************************************************************/
uint8_t http_receive(char *rq, uint16_t tcp_len, connection_desc_t *conn_desc){
	uint8_t res = 0;
	char *rs;
	int len;
	
	//printf("rq:\n%s\n", rq); //test

	parse_http_request(rq, &rs, tcp_len, conn_desc);
	
	//printf("resp:\n%s\n", rs); //test

	//send response (rs)
	len = strlen(rs);
	err_t err = netconn_write(conn_desc -> netconn_ptr,
								rs,
								len,
								NETCONN_COPY);
	if (err != ERR_OK){
		printf("data not sent\n------\n%s\n-----------\n", rs);
	}

	free(rs);
	//conn_desc -> run = CONN_STOP; //close http connection

	return res;
}


/************************************************************************
* inputs:
* 	rq - html request
* 	res - response body
* 	things - things quantity in the node
* output:
* 	error code or 1 (success)
* parse html request
************************************************************************/
int16_t parse_http_request(char *rq, 
							char **res, 
							uint16_t len, 
							connection_desc_t *conn_desc){
	int16_t status = 0;
	char *buff = NULL, *res_buff = NULL, *http_header;
	int res_len = 0;
	bool keep_alive = false;

	if(rq[0] == 'G' && rq[1] == 'E' && rq[2] == 'T'){
		//GET request
		status = get_parser(rq, &buff, root_node.things_quantity, len);
	}
	else if (rq[0] == 'P' && rq[1] == 'U' && rq[2] == 'T'){
		//PUT request
		status = put_parser(rq, &buff, root_node.things_quantity, len);
	}
	else if (rq[0] == 'P' && rq[1] == 'O' && rq[2] == 'S' && rq[3] == 'T'){
		//POST request
		status = post_parser(rq, &buff, root_node.things_quantity, len);
	}
	else if (rq[0] == 'O' && rq[1] == 'P' && rq[2] == 'T' && rq[3] == 'I' && rq[4] == 'O' &&
			rq[5] == 'N' && rq[6] == 'S'){
		//OPTIONS request
		//Cross-Origin Resource Sharing (CORS)
		status = 204;
	}
	else{
		status = 500;
	}

	if (buff != NULL){
		res_len = strlen(buff);
	}

	if ((conn_desc -> connection == CONN_HTTP_KEEP_ALIVE) ||
		(conn_desc -> connection == CONN_HTTP_RUNNING)){
		keep_alive = true;
	}
	http_header = prepare_http_header(status, keep_alive);
	
	res_buff = malloc(strlen(http_header) + res_len + 10);
	res_buff[0] = 0;
	//memset(res_buff, 0, strlen(http_header) + res_len + 10);
	strcat(res_buff, http_header);
	
	free(http_header);

	if (buff != NULL){
		strcat(res_buff, buff);
		free(buff);
	}

	*res = res_buff;

	return status;
}


/**************************************************
*
* prepare HTTP header for HTTP response
*
***************************************************/
char *prepare_http_header(int16_t status, bool keep_alive){
	char *http_header = NULL;
	int16_t len;
	
	switch(status){
	case 200:
		len = 20 + strlen(http_status_200) + strlen(h1_200);
		if (keep_alive == true){
			len += strlen(keep_alive_resp) + strlen(keep_alive_resp_param);
		}
		http_header = malloc(len);
		memset(http_header, 0, len);
		strcat(http_header, http_head);
		strcat(http_header, http_status_200);
		if (keep_alive == true){
			strcat(http_header, keep_alive_resp);
			strcat(http_header, keep_alive_resp_param);
		}
		strcat(http_header, h1_200);
		break;

	case 201:
		len = 20 + strlen(http_status_201) + strlen(h1_201);
		if (keep_alive == true){
			len += strlen(keep_alive_resp) + strlen(keep_alive_resp_param);
		}
		http_header = malloc(len);
		memset(http_header, 0, len);
		strcat(http_header, http_head);
		strcat(http_header, http_status_201);
		if (keep_alive == true){
			strcat(http_header, keep_alive_resp);
			strcat(http_header, keep_alive_resp_param);
		}
		strcat(http_header, h1_201);
		break;

	case 204:
		len = 20 + strlen(http_status_204) + strlen(h1_204);
		if (keep_alive == true){
			len += strlen(keep_alive_resp) + strlen(keep_alive_resp_param);
		}
		http_header = malloc(len);
		memset(http_header, 0, len);
		strcat(http_header, http_head);
		strcat(http_header, http_status_204);
		if (keep_alive == true){
			strcat(http_header, keep_alive_resp);
			strcat(http_header, keep_alive_resp_param);
		}
		strcat(http_header, h1_204);
		break;

	case 500:
		len = 20 + strlen(http_status_500) + strlen(h1_500);
		http_header = malloc(len);
		memset(http_header, 0, len);
		strcat(http_header, http_head);
		strcat(http_header, http_status_500);
		strcat(http_header, h1_500);
		break;

	case 400:
	default:
		len = 20 + strlen(http_status_400);
		if (keep_alive == true){
			len += strlen(keep_alive_resp) + strlen(keep_alive_resp_param);
		}
		http_header = malloc(len);
		memset(http_header, 0, len);
		strcat(http_header, http_head);
		strcat(http_header, http_status_400);
		if (keep_alive == true){
			strcat(http_header, keep_alive_resp);
			strcat(http_header, keep_alive_resp_param);
		}
		strcat(http_header, "\r\n");
	}
	
	return http_header;
}

/************************************************************************
 * inputs:
 * 		rq - request content
 *  	res - buffer address for response body
 *  	things - things quantity in the node
 * output:
 *  	0 - OK
 *     -400 - client error
 *     -500 - server error
 ***********************************************************************/
int16_t post_parser(char *rq, char **res, uint16_t things, uint16_t len){
	int16_t result = 201;
	char *ptr_1 = NULL, *ptr_2 = NULL;
	int16_t url_level_len;
	char *res_buff = NULL;
	uint8_t url_level = 0;
	char url_level_body[30];
	char *url = NULL, *org_url = NULL;

	//check if URI is absolute or not
	ptr_1 = strstr(rq, "http://");
	if (ptr_1 != NULL){
		//URI is absolute
		ptr_1 = strchr(ptr_1 + 7, '/');
	}
	else{
		ptr_1 = strchr(rq, '/');
	}
	ptr_2 = strchr(ptr_1, ' ');
	url_level_len = ptr_2 - ptr_1;

	//prepare response body
	if (url_level_len > 1){
		bool url_end = false;
		char *ptr_3 = NULL;
		bool is_number = true;
		uint16_t len = 0;
		uint8_t thing_nr = 0;

		//copy url into dedicated buffer
		url = malloc(url_level_len + 1);
		if (org_url == NULL){
			org_url = url;
		}
		memcpy(url, ptr_1, url_level_len);
		url[url_level_len] = 0;

		while (url_end == false){
			//find URL's parts
			ptr_3 = strchr(url + 1, '/');
			if (ptr_3 == NULL){
				url_end = true;
			}

			url_level++;
			switch (url_level){
			case 1:
				//thing number
				if (ptr_3 == NULL){
					len = strlen(url) - 1;
				}
				else{
					len = ptr_3 - url - 1;
				}
				for (int i = 0; i < len; i++){
					if ((url[i+1] > 0x39) && (url[i+1] < 0x30)){
						is_number = false;
						break;
					}
				}
				if (is_number == true){
					memcpy(url_level_body, url + 1, len);
					url_level_body[len] = 0;
					thing_nr = atoi(url_level_body);
					if (thing_nr < things){
						if (url_end == true){
							res_buff = get_thing(root_node.things, thing_nr, root_node.host_name,
									root_node.domain, root_node.port);
						}
					}
					else{
						url_end = true;
					}
				}
				break;

			case 2:
				//resource type {properties, actions, events}
				//e.g. POST /0/actions
				if (strstr(url, "actions") != NULL){
					//get input values from message body
					char *inputs = NULL, *action_id = NULL;
					char *ptr_4 = NULL, *ptr_5 = NULL;

					ptr_4 = strstr(rq, "\r\n\r\n");
					if (ptr_4 != NULL){
						char *p1;
						uint16_t len = 0;

						ptr_4 = strstr(ptr_4, "{\"");
						if (ptr_4 == NULL){
							goto case_2_end;
						}
						ptr_5 = strstr(ptr_4, "\":");
						if (ptr_5 == NULL){
							goto case_2_end;
						}
						len = ptr_5 - ptr_4 - 2;
						p1 = ptr_4 + 2;
						//get action ID
						action_id = malloc(len + 1);
						memset(action_id, 0, len + 1);
						memcpy(action_id, p1, len);
						//get action inputs
						ptr_5 = strstr(ptr_5, "\"input\":");
						if (ptr_5 == NULL){
							goto case_2_end;
						}
						ptr_5 = strchr(ptr_5, '{');
						if (ptr_5 == NULL){
							goto case_2_end;
						}
						ptr_4 = strchr(ptr_5, '}');
						if (ptr_5 == NULL){
							goto case_2_end;
						}
						len = ptr_4 - ptr_5;
						inputs = malloc(len); //null ended string
						memset(inputs, 0, len);
						memcpy(inputs, ptr_5 + 1, len - 1);

						int res = request_action(thing_nr, action_id, inputs);
						if (res >= 0){
							//prepare http response body
							res_buff = action_request_jsonize(thing_nr, action_id, res);
						}

						free(inputs);
						free(action_id);
					}
				}
				case_2_end:
				url_end = true;
				break;

			default:
				printf("POST parser, ERROR: URL too long\n");
				url_end = true;
			}
			url = ptr_3;
		}
	}

	if (res_buff == NULL){
		result = 400;
	}
	free(org_url);
	*res = res_buff;

	return result;
}


/************************************************************************
 * inputs:
 * 		rq		- request content
 *  	res		- buffer address for response body
 *  	things	- things quantity in the node
 *		len		- TCP length
 * output:
 *  	200		- OK
 *      400		- client error
 *      500		- server error
 ***********************************************************************/
int16_t put_parser(char *rq, char **res, uint16_t things, uint16_t tcp_len){
	int16_t result = 500;
	char *ptr_1 = NULL, *ptr_2 = NULL;
	int16_t url_level_len;
	char *res_buff = NULL;
	uint8_t url_level = 0;
	char url_level_body[30];
	char *url = NULL, *org_url = NULL;

	//check if URI is absolute or not
	ptr_1 = strstr(rq, "http://");
	if (ptr_1 != NULL){
		//URI is absolute
		ptr_1 = strchr(ptr_1 + 7, '/');
	}
	else{
		ptr_1 = strchr(rq, '/');
	}
	ptr_2 = strchr(ptr_1, ' ');
	url_level_len = ptr_2 - ptr_1;

	//prepare response body
	if (url_level_len > 1){
		bool url_end = false;
		char *ptr_3 = NULL;
		bool is_number = true;
		uint16_t len = 0;
		uint8_t thing_nr = 0;
		RESOURCE_TYPE resource = UNKNOWN;

		//copy url into dedicated buffer
		url = malloc(url_level_len + 1);
		if (org_url == NULL){
			org_url = url;
		}
		memcpy(url, ptr_1, url_level_len);
		url[url_level_len] = 0;

		while (url_end == false){
			//find URL's parts
			ptr_3 = strchr(url + 1, '/');
			if (ptr_3 == NULL){
				url_end = true;
			}

			url_level++;
			switch (url_level){
			case 1:
				//thing number
				if (ptr_3 == NULL){
					len = strlen(url) - 1;
				}
				else{
					len = ptr_3 - url - 1;
				}
				for (int i = 0; i < len; i++){
					if ((url[i+1] > 0x39) && (url[i+1] < 0x30)){
						is_number = false;
						break;
					}
				}
				if (is_number == true){
					memcpy(url_level_body, url + 1, len);
					url_level_body[len] = 0;
					thing_nr = atoi(url_level_body);
					if (thing_nr < things){
						if (url_end == true){
							res_buff = get_thing(root_node.things, thing_nr, root_node.host_name,
											root_node.domain, root_node.port);
						}
					}
					else{
						url_end = true;
					}
				}
				break;

			case 2:
				//resource type {properties, actions, events}
				//e.g. GET /0/properties
				if (strstr(url, "properties") != NULL){
					resource = PROPERTY;
				}
				else{
					resource = UNKNOWN;
					url_end = true;
				}
				break;

			case 3:
				//resource name
				//e.g. PUT /0/properties/state
				strcpy(url_level_body, url + 1);

				//get new value from message body
				char *new_value = NULL, *ptr_4 = NULL;//, *ptr_5 = NULL;
				char start_char, end_char;

				ptr_4 = strstr(rq, "\r\n\r\n");
				ptr_4 = strstr(ptr_4, url_level_body);
				if ((ptr_4 != NULL) && (rq[tcp_len-1] == '}')){
					char *p1;//, *p2;
					uint16_t buff_len = 0;
					int i;

					ptr_4 = strstr(ptr_4, ":");
					//char for json object type, fist char after ":"
					//	< " >	- string
					//	none 	- number
					//	< [ >	- array
					//	< { >	- object
					start_char = *(ptr_4 + 1);
					//find last char which closes the json object
					switch (start_char){
						case '[':
							//find "]"
							end_char = ']';
							break;
						case '{':
							//find "}"
							end_char = '}';
							break;
						case '"':
							//find '"'
							end_char = '"';
							break;
						default:
							//numbers
							end_char = 0;
					}
					if (end_char != 0){
						for (i = tcp_len-2; i > 0; i--){
							if (rq[i] == end_char){
								break;
							}
						}
					}
					else{
						i = tcp_len - 2;
					}
					buff_len = &rq[i] - ptr_4;
					p1 = ptr_4 + 1;

					new_value = malloc(buff_len + 1);
					memset(new_value, 0, buff_len + 1);
					memcpy(new_value, p1, buff_len);

					if (resource == PROPERTY){
						//call set function for this property
						result = set_resource_value(thing_nr, url_level_body, new_value);
						if (result == 200){
							res_buff = get_resource_value(thing_nr, PROPERTY, url_level_body, -1);
						}
						else if (result == 400){
							printf("http_parser ERROR: resource value not set!\n");
						}
					}

					free(new_value);
				}
				break;

			default:
				printf("PUT parser, ERROR: URL too long\n");
				url_end = true;
				result = 400;
			}
			url = ptr_3;
		}
	}
	free(org_url);
	*res = res_buff;

	return result;
}


/************************************************************************
 * GET REQUEST PARSER
 *
 * inputs:
 * 		rq - request content
 *  	res - buffer address for response body
 *  	things - things quantity in the node
 * output:
 *  	0 - OK
 *     -1 - error
 ***********************************************************************/
int16_t get_parser(char *rq, char **res, uint16_t things, uint16_t len){
	int16_t result = 200;
	char *ptr_1 = NULL, *url_end_ptr = NULL;
	int16_t url_level_len;
	char *res_buff = NULL;
	uint8_t url_level = 0;
	char url_level_body[30];
	char *url = NULL, *org_url = NULL;

	//check if URI is absolute or not
	ptr_1 = strstr(rq, "http://");
	if (ptr_1 != NULL){
		//URI is absolute
		ptr_1 = strchr(ptr_1 + 7, '/');
	}
	else{
		ptr_1 = strchr(rq, '/');
	}
	url_end_ptr = strchr(ptr_1, ' ');
	url_level_len = url_end_ptr - ptr_1;

	if ((url_level_len < 100) && (url_level_len > 0)){
		//prepare response body
		if (ptr_1[1] == 0x20){
			//GET /
			res_buff = get_root_dir();
		}
		else{
			//get something more then the node model
			bool url_end = false;
			char *ptr_3 = NULL;
			bool is_number = true;
			uint16_t len = 0;
			uint8_t thing_nr = 0;
			RESOURCE_TYPE resource = UNKNOWN;
			char index_buff[10];

			//copy url into dedicated buffer
			url = malloc(url_level_len + 1);
			memset(url, 0, url_level_len + 1);
			if (org_url == NULL){
				org_url = url;
			}
			memcpy(url, ptr_1, url_level_len);
			url[url_level_len] = 0;

			while (url_end == false){
				//find URL's parts
				ptr_3 = strchr(url + 1, '/');
				if (ptr_3 == NULL){
					url_end = true;
				}
				else{
					char *ptr_4;
					ptr_4 = strchr(ptr_3 + 1, '/');
					if (ptr_4 == NULL){
						if (strlen(ptr_3) == 1){
							//'/' at the end of url
							*ptr_3 = 0;
							url_end = true;
						}
					}
				}

				url_level++;
				switch (url_level){
				case 1:
					//thing number
					if (ptr_3 == NULL){
						len = url_level_len - 1;
					}
					else{
						len = ptr_3 - url - 1;
					}
					for (int i = 0; i < len; i++){
						if ((url[i+1] > 0x39) || (url[i+1] < 0x30)){
							is_number = false;
							break;
						}
					}
					if (is_number == true){
						memcpy(url_level_body, url + 1, len);
						url_level_body[len] = 0;
						thing_nr = atoi(url_level_body);
						if (thing_nr < things){
							if (url_end == true){
								res_buff = get_thing(root_node.things, thing_nr,
										root_node.host_name,
										root_node.domain,
										root_node.port);
							}
						}
						else{
							url_end = true; //thing number ERROR
						}
					}
					else{
					}
					break;

				case 2:
					//resource type {properties, actions, events}
					//e.g. GET /0/properties
					if (strstr(url, "properties") != NULL){
						resource = PROPERTY;
					}
					else if (strstr(url, "events") != NULL){
						resource = EVENT;
					}
					else if (strstr(url, "actions")){
						resource = ACTION;
					}
					else{
						resource = UNKNOWN;
					}

					if (url_end == true){
						res_buff = get_resource_value(thing_nr, resource, NULL, -1);
					}
					break;

				case 3:
					//resource name
					//e.g. GET /0/properties/temperature
					if ((resource == ACTION) && (url_end != true)){
						char *b = strchr(url + 1, '/');
						memcpy(url_level_body, url + 1, b - url - 1);
					}
					else{
						strcpy(url_level_body, url + 1);
					}
					if (url_end == true){
						res_buff = get_resource_value(thing_nr, resource, url_level_body, -1);
					}
					break;

				case 4:
					//resource id (used for action ID)
					memset(index_buff, 0, 10);
					strcpy(index_buff, url + 1);
					int index = atoi(index_buff);

					res_buff = get_resource_value(thing_nr, resource, url_level_body, index);
					break;

				default:
					printf("GET parser, ERROR: URL too long\n");
					url_end = true;
				}
				url = ptr_3;
			}
		}
	}

	if (res_buff == NULL){
		//TODO: correct error description
		//res_buff = get_error(org_url);
		result = 400;
	}
	free(org_url);
	*res = res_buff;

	return result;
}
