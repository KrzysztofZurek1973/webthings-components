/*
 * html_parser.c
 *  This file is a part of the "Simple Web Thing Server" project
 *  Created on: June 18, 2019
 *      Author: Krzysztof Zurek
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "http_parser.h"
#include "simple_web_thing_server.h"

extern root_node_t root_node;

int16_t get_parser(char *rq, char **res, uint16_t things);
int16_t put_parser(char *rq, char **res, uint16_t things);
int16_t post_parser(char *rq, char **res, uint16_t things);

char res_header_ok[] = "HTTP/1.1 200 OK\r\n"\
						"Content-Type: application/td+json; charset=utf-8\r\n\r\n";
char res_header_201[] = "HTTP/1.1 201 Created\r\n"\
						"Content-Type: application/json; charset=utf-8\r\n\r\n";
char res_header_err500[] = "HTTP/1.1 500 Internal Server Error\r\n"\
						"Content-Type: text/html; charset=utf-8\r\n\r\n";
char res_header_err400[] = "HTTP/1.1 400 Bad Request\r\n\r\n";


/**********************************************************************
 *
 * receive and process http request
 *
 * ********************************************************************/
uint8_t http_receive(char *rq, uint16_t tcp_len, connection_desc_t *conn_desc){
	uint8_t res = 0;
	char *rs;
	int len;

	parse_http_request(rq, &rs);

	//send response - the whole (with http header) is in "rs" buffer,
	len = strlen(rs);
	err_t err = netconn_write(conn_desc -> netconn_ptr,
						rs, len, NETCONN_COPY);
	if (err != ERR_OK){
		printf("data not sent\n------\n%s\n-----------\n", rs);
	}

	free(rs);
	conn_desc -> run = CONN_STOP; //close http connection

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
int16_t parse_http_request(char *rq, char **res){
	int16_t result = 0;
	char *buff = NULL, *res_buff = NULL, *http_header;
	int res_len = 0;

	if(rq[0] == 'G' && rq[1] == 'E' && rq[2] == 'T'){
		//GET request
		result = get_parser(rq, &buff, root_node.things_quantity);
	}
	else if (rq[0] == 'P' && rq[1] == 'U' && rq[2] == 'T'){
		//PUT request
		result = put_parser(rq, &buff, root_node.things_quantity);
	}
	else if (rq[0] == 'P' && rq[1] == 'O' && rq[2] == 'S' && rq[3] == 'T'){
		//POST request
		result = post_parser(rq, &buff, root_node.things_quantity);
	}
	else{
		result = -1;
	}

	if (buff != NULL){
		res_len = strlen(buff);
	}
	
	if (result == 0){
		http_header = res_header_ok;
	}
	else if (result == 201){
		http_header = res_header_201;
	}
	else if (result == -400){
		http_header = res_header_err400;
	}
	else{
		http_header = res_header_err500;
	}
	
	res_buff = malloc(strlen(http_header) + res_len + 10);
	res_buff[0] = 0;
	//memset(res_buff, 0, strlen(http_header) + res_len + 10);
	strcat(res_buff, http_header);

	if (buff != NULL){
		strcat(res_buff, buff);
		free(buff);
	}

	*res = res_buff;

	return result;
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
int16_t post_parser(char *rq, char **res, uint16_t things){
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
						//if (res < 0){
						//	printf("http_parser ERROR: action not executed!\n");
						//}
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
				printf("http parser, ERROR: URL too long\n");
				url_end = true;
			}
			url = ptr_3;
		}
	}

	if (res_buff == NULL){
		result = -400;
	}
	free(org_url);
	*res = res_buff;

	return result;
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
int16_t put_parser(char *rq, char **res, uint16_t things){
	int16_t result = 0;
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
				char *new_value = NULL, *ptr_4 = NULL, *ptr_5 = NULL;

				ptr_4 = strstr(rq, "\r\n\r\n");
				ptr_4 = strstr(ptr_4, url_level_body);
				if (ptr_4 != NULL){
					char *p1, *p2;
					uint16_t len = 0;

					ptr_4 = strstr(ptr_4, ":");
					ptr_5 = strchr(ptr_4, '\"');
					if (ptr_5 != NULL){
						p2 = strchr(ptr_5 + 1, '\"');
						len = p2 - ptr_5 - 1;
						p1 = ptr_5 + 1;
					}
					else{
						p2 = strchr(ptr_4 + 1, '}');
						len = p2 - ptr_4 - 1;
						p1 = ptr_4 + 1;
					}

					new_value = malloc(len + 1);
					memset(new_value, 0, len + 1);
					memcpy(new_value, p1, len);

					if (resource == PROPERTY){
						//call set function for this property
						int8_t res = set_resource_value(thing_nr, url_level_body, new_value);
						if (res < 0){
							printf("http_parser ERROR: resource value not set!\n");
						}
						else{
							res_buff = get_resource_value(thing_nr, PROPERTY, url_level_body, -1);
						}
					}

					free(new_value);
				}
				break;

			default:
				printf("http parser, ERROR: URL too long\n");
				url_end = true;
			}
			url = ptr_3;
		}
	}

	if (res_buff == NULL){
		result = -400;
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
int16_t get_parser(char *rq, char **res, uint16_t things){
	int16_t result = 0;
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
					printf("http parser, ERROR: URL too long\n");
					url_end = true;
				}
				url = ptr_3;
			}
		}
	}

	if (res_buff == NULL){
		//TODO: correct error description
		//res_buff = get_error(org_url);
		result = -400;
	}
	free(org_url);
	*res = res_buff;

	return result;
}
