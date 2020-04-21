/*
 * simple_web_thing_server.c
 *
 *  Created on: July 1, 2019
 *      Author: Krzysztof Zurek
 *      Notes: some code is inspired by
 *      http://www.barth-dev.de/websockets-on-the-esp32/#
 *
 *      Web Thing Server implementation for ESP32 chips and ESP-IDF
 *      Compliant with Mozilla-IoT API (almost in 100%)
 */

#include <stdio.h>
#include <sys/param.h>
#include <string.h>
#include <stdbool.h>

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

#define MAX_PAYLOAD_LEN		1024
//#define SHA1_RES_LEN		20	//sha1 result length
#define CLOSE_TIMEOUT_MS	2000 //ms

#define WS_UPGRADE "Upgrade: websocket"

//global server variables
static xTaskHandle server_task_handle;
static struct netconn *server_conn;
root_node_t root_node; //http parser uses it
connection_desc_t connection_tab[MAX_OPEN_CONN];

//functions
int8_t send_websocket_msg(thing_t *t, char *buff, int len);


/*********************************************************
*
* close TCP connection and clear connection resources
*
*********************************************************/
int8_t close_thing_connection(connection_desc_t *conn_desc, char *tag){
	struct netconn *conn_ptr;
	err_t err;
	uint8_t index;
	
	index = conn_desc -> index;
	conn_ptr = conn_desc -> netconn_ptr;
	printf("%s: CLOSE CONNECTION, index = %i, type: %i\n",
			tag,
			index,
			conn_desc -> type);

	if (conn_ptr != NULL){
		//close TCP connection
		if ((err = netconn_close(conn_ptr)) != ERR_OK){
			printf("%s\"netconn_close\" ERROR: %i\n", tag, err);
		}
	
		if (netconn_delete(conn_ptr) != ERR_OK){
			printf("%s\"netconn_delete\" ERROR: %i\n", tag, err);
		}
		//delete subscriber
		if (conn_desc -> type == CONN_WS){
			delete_subscriber(conn_desc);
		}
	}
	//clear record in connection table
	memset(conn_desc, 0, sizeof(connection_desc_t));
	return 1;
}


/***************************************************************************
 *
 * task where data is received and processed (both http and websocket)
 *
 * ************************************************************************/
static void connection_task(void *arg){
	int8_t index;
	err_t net_err = ERR_OK;
	struct netconn *conn_ptr;
	struct netbuf *inbuf;
	uint16_t tcp_len = 0;
	char *rq = NULL;
	connection_desc_t *conn_desc;

	conn_desc = (connection_desc_t *)arg;
	index = conn_desc -> index;

	//printf("receive task starting, index: %i\n", index);
	conn_ptr = conn_desc -> netconn_ptr;

	while(1){
		net_err = netconn_recv(conn_ptr, &inbuf);
		if (net_err == ERR_OK){
			//read data from input buffer
			netbuf_data(inbuf, (void**) &rq, &tcp_len);

			if (conn_desc -> type == CONN_UNKNOWN){
				if (strstr(rq, WS_UPGRADE) != NULL){
					conn_desc -> type = CONN_WS;
				}
				else{
					conn_desc -> type = CONN_HTTP;
				}
			}

			if (conn_desc -> type == CONN_HTTP){
				//test
				//printf("CONNECTION TASK HTTP\n%s\n", rq);
				//http connection, call http parser
				http_receive(rq, tcp_len, conn_desc);
			}
			else{
				//test---------------------------------
				//printf("CONNECTION TASK WS, len: %i\n", tcp_len);
				//uint8_t *c;
				
				//c = (uint8_t *)rq;
				//for (int i = 0; i < tcp_len; i++){
				//	printf("%02hhX ", *c++);
				//}
				//printf("\n");
				//end of test----------------------------
				
				//websocket connection
				ws_receive(rq, tcp_len, conn_desc);
			}
		}
		else{
			//connection is closed
			conn_desc -> run = CONN_STOP;
			if (net_err == ERR_CLSD){
				printf("TCP was closed by client\n");
			}
			else{
				printf("\"netconn_recv\" index: %i, ERROR = %i\n", index, net_err);
			}

		}
		netbuf_free(inbuf);
		netbuf_delete(inbuf);
		if (conn_desc -> run == CONN_STOP){
			break;
		}
	}//while

	close_thing_connection(conn_desc, "CONN_TASK");

	vTaskDelete(NULL);
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
	int8_t index;

	cfg = (server_cfg_t *)arg;
	port = cfg -> port;

	//set up new TCP listener
	server_conn = netconn_new(NETCONN_TCP);
	netconn_bind(server_conn, NULL, port);
	netconn_listen(server_conn);
	printf("Web Thing Server is in listening mode\n");
	vTaskDelay(1000 / portTICK_PERIOD_MS); //wait 1 sec

	for (;;){
		if (netconn_accept(server_conn, &newconn) == ERR_OK){
			//check if there is a place for next client
			index = -1;
			//printf("new client connected\n");
			for (int i = 0; i < MAX_OPEN_CONN; i++){
				if (connection_tab[i].netconn_ptr == NULL){
					index = i;
					break;
				}
			}
			if (index > -1){
				//printf("client will be served, index: %i\n", index);
				connection_tab[index].type = CONN_UNKNOWN;
				connection_tab[index].netconn_ptr = newconn;
				connection_tab[index].task_handl = NULL;
				connection_tab[index].conn_state = WS_CLOSED;
				connection_tab[index].timer_handl = NULL;
				connection_tab[index].index = index;
				connection_tab[index].ws_pings = 0;
				connection_tab[index].ws_pongs = 0;
				connection_tab[index].run = CONN_RUN;
				connection_tab[index].thing = NULL;
				connection_tab[index].bytes = 0;

				BaseType_t xret;
				xret = xTaskCreate(connection_task, "conn_task", 1024*6,
							&connection_tab[index], 1,
							&connection_tab[index].task_handl);
				if (xret != pdPASS){
					printf("new connection failed\n");
					connection_tab[index].netconn_ptr = NULL;
					netconn_close(newconn);
					netconn_delete(newconn);
				}
			}
			else{
				//too much clients, send error info and close connection
				printf("no space for new clients\n");
				netconn_close(newconn);
				netconn_delete(newconn);
			}
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
int8_t set_resource_value(int8_t thing_nr, char *name, char *new_value_str){
	thing_t *t = NULL;
	property_t *p = NULL;
	int8_t set_result = -1;

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
		}
	}

	return set_result;
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
				buff = malloc(PROP_VAL_LEN * n);
				memset(buff, 0, PROP_VAL_LEN * n);
				buff[0] = '{';
				p = t -> properties;
				for (int i = 0; i < n; i++){
					buff1 = p -> value_jsonize(p);
					strcpy(buff + strlen(buff), buff1);
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
					//take mux
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
					//release mux
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

	//printf("thing \"%s\" added to server, there is(are) %i thing(s)\n",
	//		t -> name, root_node.things_quantity);
	return res;
}

//**********************************************************************
//get the root directory
char *get_root_dir(){
	//char *buff_1 = NULL;
	char *res_buff = NULL;
	thing_t *t = NULL;

	//create model of the whole node ----------------------------------
	t = root_node.things;
	if (root_node.things_quantity == 0){
		res_buff = malloc(6);
		strcat(res_buff, "[{}]");
	}
	else if (root_node.things_quantity == 1){
		//single thing node
		res_buff = malloc(t -> model_len + 5);
		//strcat(res_buff, "[{");
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
				//strcat(res_buff, buff_1);
				//free(buff_1);
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


/***************************************************
*
* every 10 sec announce webthing service
*
****************************************************/
/*
static void mdns_announcement_task(void* arg){
	uint32_t i = 0;
	
	for(;;){
		vTaskDelay(30000 / portTICK_PERIOD_MS); //wait 30 sec
		i++;
		printf("mDNS task, %i\n", i);
		//trigger mDNS announcment
		mdns_service_port_set("_webthing", "_tcp", root_node.port);
	}
}
*/


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
	
	//create mDNS announcement task
	//xTaskCreate(mdns_announcement_task,
	//			"mdns_task",
	//			1024*2, NULL, 0, NULL);

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
		printf("event msg: %s\n", buff);
		
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
