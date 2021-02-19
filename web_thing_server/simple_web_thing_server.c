/*
 * simple_web_thing_server.c
 *
 *  Created on: July 1, 2019
 *  Last edit:	Feb 19, 2021
 *      Author: Krzysztof Zurek
 *		e-mail: krzzurek@gmail.com
 *
 *      Notes: some code is inspired by
 *      http://www.barth-dev.de/websockets-on-the-esp32/#
 *
 *      Web Thing Server implementation for ESP32 chips and ESP-IDF
 *      Compliant with WebThings API (almost in 100%)
 */

#include <stdio.h>
#include <sys/param.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "lwip/api.h"
#include "mdns.h"

#include "simple_web_thing_server.h"
#include "websocket.h"
#include "http_parser.h"
#include "common.h"

#define WS_UPGRADE "Upgrade: websocket"
#define KEEP_ALIVE_TIMEOUT 2000

//global server variables
static xTaskHandle server_task_handle;
static struct netconn *server_conn;
root_node_t root_node; //http parser uses it
connection_desc_t connection_tab[MAX_OPEN_CONN];
static xSemaphoreHandle connection_mux = NULL;
static xSemaphoreHandle server_mux = NULL;

//functions
int8_t send_websocket_msg(thing_t *t, char *buff, int len);
void http_timer_fun(TimerHandle_t xTimer);

/*****************************************************
*
* get server time in format: 2020/05/20 13:07:15
*
*******************************************************/
void get_server_time(char *time_buff, uint32_t buff_len){
	time_t now;
	struct tm timeinfo;
	
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(time_buff, buff_len, "%Y/%m/%d %H:%M:%S", &timeinfo);
}


/*********************************************************
*
* close TCP connection and clear connection resources
*
*********************************************************/
int8_t close_thing_connection(connection_desc_t *conn_desc, char *tag){
	struct netconn *conn_ptr;
	err_t err;
	char time_buffer[20];
	
	//printf("%s, CONN delete ID: %i\n", tag, conn_desc -> index);
	
	if (conn_desc -> netconn_ptr != NULL){
		xSemaphoreTake(server_mux, portMAX_DELAY);
		
		//delete subscriber
		if (conn_desc -> type == CONN_WS){
			delete_subscriber(conn_desc);
		}
		//delete timer
		if (conn_desc -> timer != NULL){
			xTimerDelete(conn_desc -> timer, 0);
			conn_desc -> timer = NULL;
			//printf("timer deleted\n");
		}
		conn_ptr = conn_desc -> netconn_ptr;
		conn_desc -> netconn_ptr = NULL;
		
		xSemaphoreGive(server_mux);
	
		if (conn_ptr != NULL){
			get_server_time(time_buffer, sizeof(time_buffer));
			//close TCP connection
			if ((err = netconn_close(conn_ptr)) != ERR_OK){
				printf("%s \"netconn_close\" ERROR: %i\n", tag, err);
			}
		
			if ((err = netconn_delete(conn_ptr)) != ERR_OK){
				printf("%s \"netconn_delete\" ERROR: %i\n", tag, err);
			}
		}
	}
	
	return 1;
}


/***************************************************************************
 *
 * task where data is received and processed (both http and websocket)
 *
 * ************************************************************************/
static void connection_task(void *arg){
	err_t net_err = ERR_OK;
	struct netconn *conn_ptr;
	struct netbuf *inbuf;
	uint16_t tcp_len = 0;
	char *rq = NULL;
	connection_desc_t *conn_desc;
	bool run = true;

	conn_desc = (connection_desc_t *)arg;

	//printf("start connection, ID: %i\n", conn_desc -> index);
	conn_ptr = conn_desc -> netconn_ptr;

	while(run){
		net_err = netconn_recv(conn_ptr, &inbuf);
		if (net_err == ERR_OK){
			conn_desc -> requests++;
			//read data from input buffer
			net_err = netbuf_data(inbuf, (void**) &rq, &tcp_len);

			if (net_err == ERR_OK){
				//printf("tcp_len: %i\n", tcp_len); //TEST
				
				if (conn_desc -> type == CONN_UNKNOWN){
					//check connection type: HTTP or websocket
					if (strstr(rq, WS_UPGRADE) != NULL){
						//conection is websocket
						conn_desc -> type = CONN_WS;
						conn_desc -> connection = CONN_WS_RUNNING;
					}
					else{
						//connection is HTTP
						conn_desc -> type = CONN_HTTP;
						if (conn_desc -> connection == CONN_STATE_UNKNOWN){
							//check HTTP request type (keep-alive or close)
							char buff[15], *q;
							
							q = strstr(rq, "Connection:");
							if (q != NULL){
								q += 11;
								while(*++q == ' ');
								char *q1 = strstr(q, "\r\n");
								int len = q1 - q;
								if (len > 14){
									len = 14;
								}
								memcpy(buff, q, len);
								buff[len] = 0;
								if (strstr(buff, "keep-alive") != NULL){
									conn_desc -> connection = CONN_HTTP_KEEP_ALIVE;
								}
								else{
									conn_desc -> connection = CONN_HTTP_CLOSE;
								}
							}							
						}
					}
				}

				if (conn_desc -> type == CONN_HTTP){
					//parse http connection
					http_receive(rq, tcp_len, conn_desc);
				}
				else{
					//parse websocket connection
					ws_receive(rq, tcp_len, conn_desc);
				}
					
				
				if (conn_desc -> connection == CONN_HTTP_KEEP_ALIVE){
					conn_desc -> connection = CONN_HTTP_RUNNING;
					//start timer
					conn_desc -> timer = xTimerCreate("http_timer",
								pdMS_TO_TICKS(KEEP_ALIVE_TIMEOUT),
								pdFALSE,
								(void *)&conn_desc -> index,
								http_timer_fun);
					if (conn_desc -> timer != NULL){
						BaseType_t res = xTimerStart(conn_desc -> timer, 5);
						if (res != pdPASS) {
							printf("HTTP timer start failed\n");
							run = false;
							xTimerDelete(conn_desc -> timer, 10);
						}
					}
				}
				else if (conn_desc -> connection == CONN_HTTP_CLOSE){
					run = false;
				}
				else if (conn_desc -> connection == CONN_WS_CLOSE){
					run = false;
				}
			}
			else{
				printf("netbuff data ERROR\n");
			}
		}
		else{
			//connection is closed
			run = false;
			/*
			printf("conn will be deleted, ID: %i\n", conn_desc -> index);

			char time_buffer[20];	
				
			get_server_time(time_buffer, sizeof(time_buffer));
				
			if (net_err == ERR_CLSD){
				printf("%s, TCP was closed by client\n", time_buffer);
			}
			else{
				printf("%s, \"netconn_recv\" index: %i, ERROR = %i\n",
						time_buffer, index, net_err);
			}
			*/
		}
		//free receive buffer
		if (inbuf != NULL){
			netbuf_free(inbuf);
			netbuf_delete(inbuf);
		}
	}//while

	if (conn_desc -> netconn_ptr != NULL){
		close_thing_connection(conn_desc, "CONN_TASK");
	}

	vTaskDelete(NULL);
}


/*****************************************
 *
 * HTTP timer function
 *
 ******************************************/
void http_timer_fun(TimerHandle_t xTimer){
	uint32_t id;
	connection_desc_t *conn_desc;
	
	id = *(uint32_t *)pvTimerGetTimerID(xTimer); 
	
	conn_desc = &connection_tab[id];
	if (conn_desc -> netconn_ptr != NULL){
		close_thing_connection(conn_desc, "TIMER_TASK");
	}
}


/****************************************************************************
 *
 * main server function, new TCP connection comes here
 *
 * ***************************************************************************/
static void server_main_task(void* arg){
	server_cfg_t *cfg;
	uint16_t port;
	struct netconn *newconn;
	int8_t index = -1;

	cfg = (server_cfg_t *)arg;
	port = cfg -> port;
	
	//cennection mutex
	connection_mux = xSemaphoreCreateMutex();
	server_mux = xSemaphoreCreateMutex();

	//set up new TCP listener
	server_conn = netconn_new(NETCONN_TCP);
	netconn_bind(server_conn, NULL, port);
	netconn_listen(server_conn);
	printf("Web Thing Server in listening mode\n");
	vTaskDelay(1000 / portTICK_PERIOD_MS); //wait 1 sec

	for (;;){
		if (netconn_accept(server_conn, &newconn) == ERR_OK){
			//check if there is a place for next client
			index = -1;
			xSemaphoreTake(server_mux, portMAX_DELAY);
			
			for (int i = 0; i < MAX_OPEN_CONN; i++){
				if (connection_tab[i].netconn_ptr == NULL){
					index = i;
					break;
				}
			}
			
			if (index > -1){
				connection_tab[index].type = CONN_UNKNOWN;
				connection_tab[index].netconn_ptr = newconn;
				connection_tab[index].task_handl = NULL;
				connection_tab[index].ws_state = WS_CLOSED;
				connection_tab[index].timer = NULL;
				connection_tab[index].index = index;
				connection_tab[index].ws_pings = 0;
				connection_tab[index].ws_pongs = 0;
				connection_tab[index].connection = CONN_STATE_UNKNOWN;
				connection_tab[index].thing = NULL;
				connection_tab[index].bytes = 0;
				connection_tab[index].requests = 0;
				connection_tab[index].mutex = connection_mux;
				
				xSemaphoreGive(server_mux);
				
				BaseType_t xret;
				xret = xTaskCreate(connection_task, "conn_task",
									1024*6,
									&connection_tab[index],
									1,
									&connection_tab[index].task_handl);
				if (xret != pdPASS){
					printf("new connection failed\n");
					connection_tab[index].netconn_ptr = NULL;
					netconn_close(newconn);
					netconn_delete(newconn);
				}
			}
			else{
				xSemaphoreGive(server_mux);
				//too much clients, send error info and close connection
				printf("no space for new clients\n");
				netconn_close(newconn);
				netconn_delete(newconn);
			}
			
			//TEST
			//char time_buffer[20];
			//get_server_time(time_buffer, sizeof(time_buffer));
			//printf("%s, new client connected, index: %i\n", time_buffer, index);
			//END OF TEST
		}
	}
}


/*************************************************************************
 * return thing address for given thing_nr
 * ***********************************************************************/
thing_t *get_thing_ptr(uint8_t thing_nr){
	thing_t *t;

	//find thing
	t = root_node.things;
	while (t != NULL){
		if (t -> thing_nr == thing_nr){
			break;
		}
		t = t -> next;
	}

	return t;
}

/*************************************************************************
*
* request action
*	out: request index
*
* ************************************************************************/
int request_action(int8_t thing_nr, char *action_id, char *inputs){
	thing_t *t = NULL;
	action_t *a = NULL;
	int out_index = -1;
	int8_t run_result = -1;

	//find thing
	t = get_thing_ptr(thing_nr);
	//find action
	if (t != NULL){
		a = get_action_ptr(t, action_id);

		//run action
		if (a != NULL){
			if (a -> running_request_index == -1){
				run_result = a -> run(inputs);
				if (run_result >= 0){
					//add request to the list
					out_index = add_request_to_list(a, inputs);
				}
			}
		}

	}

	return out_index;
}

/*************************************************************************
*
* set resource value
*
* ************************************************************************/
int16_t set_resource_value(int8_t thing_nr, char *name, char *new_value_str){
	thing_t *t = NULL;
	property_t *p = NULL;
	int16_t set_result = 0;
	int16_t result = 500;

	//find thing
	t = get_thing_ptr(thing_nr);
	
	//find property
	if (t != NULL){
		p = t -> properties;
		while (p != NULL){
			if (strcmp(p -> id, name) == 0){
				break;
			}
			p = p -> next;
		}

		//set new value for this property
		if ((p != NULL) && (p -> read_only == false)){
			set_result = p -> set(new_value_str);
		}

		if (set_result == 1){
			inform_all_subscribers_prop(p);
			result = 200;
		}
		else if (set_result == 0){
			result = 200;
		}
		else if (set_result == -1){
			result = 400;
		}	
	}

	return result;
}


/*************************************************************************
*
* prepare values of resources, if name = NULL, send values for all resources
* (properties only)
*
* ************************************************************************/
char *get_resource_value(int8_t thing_nr, RESOURCE_TYPE resource, char *name, int index){
	char *buff = NULL, *buff1;
	thing_t *t;
	property_t *p;
	uint16_t n;
	int len = -1, curr_len = 0, temp_len = 0;

	//find thing
	t = get_thing_ptr(thing_nr);

	if (t != NULL){
		//values of all resources
		switch (resource){
		// -----------------------------------------------------------
		case PROPERTY:
			if (name == NULL){
				//send values of all properties
				n = t -> prop_quant;
				len = PROP_VAL_LEN * n;
				buff = malloc(PROP_VAL_LEN * n);
				memset(buff, 0, PROP_VAL_LEN * n);
				buff[0] = '{';
				p = t -> properties;
				for (int i = 0; i < n; i++){
					buff1 = p -> value_jsonize(p);
					temp_len = strlen(buff1);
					if (curr_len + temp_len >= (len - 5)){
						//allocate new buffer
						len = curr_len + temp_len + 10;
						char *new_buff = malloc(len);
						memset(new_buff, 0, len);
						strcpy(new_buff, buff);
						free(buff);
						buff = new_buff;
					}
					curr_len += temp_len;
					strcat(buff, buff1);
					free(buff1);
					if (i < (n - 1)){
						//comma before the next property
						strcat(buff, ",");
						p = p -> next;
					}
				}
				strcat(buff, "}");
			}
			else{
				//send value of one particular property
				p = t -> properties;
				while (p != NULL){
					if (strcmp(name, p -> id) == 0){
						break;
					}
					p = p -> next;
				}
				if (p != NULL){
					buff = malloc(PROP_VAL_LEN);
					memset(buff, 0, PROP_VAL_LEN);
					buff[0] = '{';
					buff1 = p -> value_jsonize(p);
					temp_len = strlen(buff1);
					if (temp_len >= (PROP_VAL_LEN - 5)){
						//allocate new buffer
						len = curr_len + temp_len + 10;
						char *new_buff = malloc(len);
						memset(new_buff, 0, len);
						strcpy(new_buff, buff);
						free(buff);
						buff = new_buff;
					}
					strcpy(buff + 1, buff1);
					free(buff1);
					buff[strlen(buff)] = '}';
				}
			}
			break;

		//-------------------------------------------------------------
		case ACTION:
			if (name == NULL){
				//list all action requests for this thing
				action_t *a = t -> actions;
				if (a != NULL){
					//count all requests in queues
					int ar_cnt = 0;
					action_t *a1 = t -> actions;

					while (a1 != NULL){
						action_request_t *ar = a1 -> requests_list;
						while (ar != NULL){
							ar_cnt++;
							ar = ar -> next;
						}
						a1 = a1 -> next;
					}

					buff = malloc(300 * ar_cnt + 3);
					memset(buff, 0, 300 * ar_cnt + 3);
					strcat(buff, "[");

					int m;
					if (ar_cnt < 5){
						m = ar_cnt;
					}
					else{
						m = 5;
					}
					char *buff_temp = malloc(m * 300);
					memset(buff_temp, 0, m * 300);
					int first_item = 0;
					uint16_t b = 0;
					while (a != NULL){
						b = get_action_request_queue(a, buff_temp);
						if (b > 2){
							if (first_item != 0){
								strcat(buff, ",");
							}
							strcat(buff, buff_temp);
						}
						first_item++;
						a = a -> next;
						if (a != NULL){
							memset(buff_temp, 0, m * 300);
						}
					}
					free(buff_temp);
					strcat(buff, "]");
				}
				else{
					buff = malloc(3);
					buff[0] = '[';
					buff[1] = ']';
					buff[2] = 0;
				}
			}
			else{
				action_t *a = get_action_ptr(t, name);
				if (a != NULL){
					if (index < 0){
						//prepare list of all requests for particular action
						buff = malloc(300 * MAX_ACTION_REQUESTS);
						memset(buff, 0, 300 * MAX_ACTION_REQUESTS);
						strcat(buff, "[");
						get_action_request_queue(a, buff + 1);
						strcat(buff, "]");
					}
					else{
						//send info about one particular action
						buff = action_request_jsonize(t -> thing_nr, name, index);
					}
				}
			}
			break;

		//-------------------------------------------------------------
		case EVENT:
			buff = event_list_jsonize(t -> thing_nr, name);
			break;

		//-------------------------------------------------------------
		case UNKNOWN:
			break;
		}
	}

	return buff;
}


//add thing to root node
int8_t add_thing_to_server(thing_t *t){
	int8_t res = 0;

	if (root_node.things_quantity == 0){
		root_node.things = t;
		root_node.last_thing = t;
	}
	else{
		root_node.last_thing -> next = t;
		root_node.last_thing = t;
	}
	t -> thing_nr = root_node.things_quantity;
	root_node.things_quantity++;

	return res;
}

//**********************************************************************
//get the root directory
char *get_root_dir(){
	char *res_buff = NULL;
	thing_t *t = NULL;

	//create model of the whole node ----------------------------------
	t = root_node.things;
	if (root_node.things_quantity == 0){
		res_buff = malloc(6);
		memset(res_buff, 0, 6);
		strcat(res_buff, "[{}]");
	}
	else if (root_node.things_quantity == 1){
		//single thing node
		res_buff = malloc(t -> model_len + 5);
		res_buff[0] = '[';
		res_buff[1] = '{';
		res_buff[2] = 0;
		thing_jsonize(t, root_node.host_name, root_node.domain,
								root_node.port, res_buff + 2);
		strcat(res_buff, "}]");
	}
	else{
		//multithing node
		//count length of things model
		int len = 0;
		t = root_node.things;
		while (t != NULL){
			len += t -> model_len;
			t = t -> next;
		}
		uint16_t things = root_node.things_quantity;
		res_buff = malloc(len + things * 5);
		res_buff[0] = '[';
		res_buff[1] = '{';
		res_buff[2] = 0;
		char *buff_start = res_buff + 2;

		t = root_node.things;
		for (int i = 0; i < things; i++){
			if (t != NULL){
				int t_len = thing_jsonize(t, root_node.host_name, root_node.domain,
										root_node.port, buff_start);

				if (i != things - 1){
					strcat(res_buff, "},{");
					buff_start += 3;
				}
				t = t -> next;
				buff_start += t_len;
			}
		}
		strcat(res_buff, "}]");
	}

	return res_buff;
}


// ***************************************************************************
int8_t root_node_init(void){
	int res = 0;

	root_node.last_thing = NULL;
	root_node.things = NULL;
	root_node.things_quantity = 0;

	return res;
}


// **************************************************************************
int8_t start_web_thing_server(uint16_t port, char *host_name, char *domain){
	int8_t res = 0;
	server_cfg_t cfg;

	printf("\"Simple Web Thing Server\" is starting now\n");
	//clear connection table
	memset(connection_tab, 0, MAX_OPEN_CONN * sizeof(connection_desc_t));

	root_node.port = port;
	strcpy(root_node.host_name, host_name);
	strcpy(root_node.domain, domain);

	cfg.port = port;
	xTaskCreate(server_main_task, "server_main_task", 1024*10, &cfg, 1, &server_task_handle);

	//initialize websocket server
	ws_server_init(port);
	printf("websocket server started\n");

	return res;
}


/*****************************************************************************
 *
 * inform all websocket clients (subscribers) about new value of property
 *
 * ***************************************************************************/
int8_t inform_all_subscribers_prop(property_t *_p){
	int8_t res = -1;
	int len;
	char *json_value, *buff;
	char msg[] = "{\"messageType\":\"propertyStatus\",\"data\":{%s}}";
	subscriber_t *s;
	ws_queue_item_t *queue_data;

	//prepare message
	json_value = _p -> value_jsonize(_p);
	len = strlen(json_value) + strlen(msg);

	s = _p -> t -> subscribers;
	while (s != NULL){
		buff = malloc(len + 1);
		sprintf(buff, msg, json_value);
		
		queue_data = malloc(sizeof(ws_queue_item_t));
		queue_data -> payload = (uint8_t *)buff;
		queue_data -> len = strlen(buff);
		queue_data -> opcode = WS_OP_TXT;
		queue_data -> ws_frame = 0x1;
		queue_data -> text = 0x1;
		queue_data -> conn_desc = s -> conn_desc;
		ws_send(queue_data, 1000);
		s = s -> next;
		res = 0;
	}
	
	free(json_value);

	return res;
}


/*****************************************************************************
 *
 * inform all websocket clients (subscribers) about action status change
 * inputs:
 * 		a - action
 * 		data - data to be send (with curly bracket)
 * 		len - length of data
 *
 * ***************************************************************************/
int8_t inform_all_subscribers_action(action_t *_a, char *data, int len){
	int8_t res = -1;
	//int len;
	char *buff;
	char msg[] = "{\"messageType\":\"actionStatus\",\"data\":%s}";
	subscriber_t *s;
	ws_queue_item_t *queue_data;

	s = _a -> t -> subscribers;
	while (s != NULL){
		//prepare message
		buff = malloc(len + strlen(msg) + 1);
		sprintf(buff, msg, data);
		
		queue_data = malloc(sizeof(ws_queue_item_t));
		queue_data -> payload = (uint8_t *)buff;
		queue_data -> len = strlen(buff);
		queue_data -> opcode = WS_OP_TXT;
		queue_data -> ws_frame = 0x1;
		queue_data -> text = 0x1;
		queue_data -> conn_desc = s -> conn_desc;
		ws_send(queue_data, 1000);
		s = s -> next;
		res = 0;
	}

	return res;
}


/*****************************************************************************
 *
 * inform all websocket clients (subscribers) about action status change
 * inputs:
 * 		e - event
 * 		data - data to be send (with curly bracket)
 * 		len - length of data
 *
 * ***************************************************************************/
int8_t inform_all_subscribers_event(event_t *_e, char *data, int len){
	int8_t res = -1;
	char *buff;
	char msg[] = "{\"messageType\":\"event\",\"data\":{%s}}";
	subscriber_t *s;
	ws_queue_item_t *queue_data;

	s = _e -> t -> subscribers;
	while (s != NULL){
		//prepare message
		buff = malloc(len + strlen(msg) + 1);
		sprintf(buff, msg, data);
		queue_data = malloc(sizeof(ws_queue_item_t));
		queue_data -> payload = (uint8_t *)buff;
		queue_data -> len = strlen(buff);
		queue_data -> opcode = WS_OP_TXT;
		queue_data -> ws_frame = 0x1;
		queue_data -> text = 0x1;
		queue_data -> conn_desc = s -> conn_desc;
		ws_send(queue_data, 1000);
		s = s -> next;
		res = 0;
	}

	return res;
}
