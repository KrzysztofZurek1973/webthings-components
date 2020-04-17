/*
 * web_thing_softap.c
 *
 *  Created on: Oct 3, 2019
 *      Author: kz
 */
#include <stdio.h>
#include <sys/param.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
//#include "esp_event_loop.h"
#include "esp_event.h"
#include "mdns.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_spi_flash.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "common.h"
#include "web_thing_mdns.h"
#include "web_thing_softap.h"

//wifi AP data
#define NODE_WIFI_SSID      "iot-node-ap"
#define NODE_WIFI_PASS      "htqn9Fzv"
#define MAX_WIFI_STA_CONN	1

const char *WIFI_AP_TAG = "wifi softAP";
static EventGroupHandle_t ap_wifi_event_group;
static void ap_event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data);

char ap_get_page[] = "HTTP/1.1 200 OK\r\n"\
					"Content-Type: text/html; charset=utf-8\r\n\r\n"\
					"<!DOCTYPE html>"\
					"<html>"\
					"<head><meta content=\"text/html\" charset=\"utf-8\">"\
					"<title>SET WIFI PARAMETERS</title></head>"\
					"<body>"\
					"<form id=\"form1\" method=\"post\">"\
					"<div align=\"center\">"\
					"WiFi SSID (network name): <input type=\"text\" name=\"ssid\"><br><br>"\
					"WiFi password: <input type=\"password\" name=\"pass\"><br><br>"\
					"Node name: <input type=\"text\" name=\"mdns_host\"><br><br>"\
					"<input type=\"submit\" value=\"Submit\">"\
					"</div>"\
					"</form>"\
					"</body></html>";

char ap_res_header_ok[] = "HTTP/1.1 204 NO CONTENT\r\n";

/*****************************************************************
 *
 * initialize wifi software Access Point
 *
 * ***************************************************************/
void wifi_init_softap()
{
    ap_wifi_event_group = xEventGroupCreate();

	tcpip_adapter_init();
    //esp_netif_init();
    //ESP_ERROR_CHECK(esp_event_loop_init(ap_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    //esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = NODE_WIFI_SSID,
            .ssid_len = strlen(NODE_WIFI_SSID),
            .password = NODE_WIFI_PASS,
            .max_connection = MAX_WIFI_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(NODE_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_AP_TAG, "wifi_init_softap finished. SSID:%s password:%s",
             NODE_WIFI_SSID, NODE_WIFI_PASS);
}


/***************************************************************
 *
 *
 *
 * **************************************************************/
static void ap_event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data){ 

	if (event_base == WIFI_EVENT){
		if (event_id == WIFI_EVENT_AP_STACONNECTED){
	    		//ESP_LOGI(WIFI_AP_TAG, "station connected");
	    		wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        		ESP_LOGI(WIFI_AP_TAG, "station "MACSTR" join, AID=%d",
                		MAC2STR(event->mac), event->aid);
	    }
		else if (event_id == WIFI_EVENT_AP_STADISCONNECTED){
	    		//ESP_LOGI(WIFI_AP_TAG, "station disconnected");
	    		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        		ESP_LOGI(WIFI_AP_TAG, "station "MACSTR" leave, AID=%d",
                		MAC2STR(event->mac), event->aid);
	    }
		else{
			ESP_LOGI(WIFI_AP_TAG, "unknown event");
	    }
    }
}


/****************************************************************************
 *
 * main server function, new TCP connection comes here
 *
 * ***************************************************************************/
void ap_server_task(void* arg){
	uint16_t port = 8080;
	struct netconn *newconn;
	err_t net_err = ERR_OK;
	struct netbuf *inbuf;
	uint16_t tcp_len = 0;
	char *rq = NULL;
	int counter = 0;
	struct netconn *server_conn;
	//bool *node_restart;

	//node_restart = (bool *)arg;
	//set up TCP listener
	server_conn = netconn_new(NETCONN_TCP);
	netconn_bind(server_conn, NULL, port);
	netconn_listen(server_conn);
	printf("AP Server in listening mode\n");
	vTaskDelay(1000 / portTICK_PERIOD_MS); //wait 1 sec

	for (;;){
		if (netconn_accept(server_conn, &newconn) == ERR_OK){

			//***************
			net_err = netconn_recv(newconn, &inbuf);
			if (net_err == ERR_OK){
				//read data from input buffer
				netbuf_data(inbuf, (void**) &rq, &tcp_len);

				printf("received request:\n%s\n", rq);

				if(rq[0] == 'G' && rq[1] == 'E' && rq[2] == 'T'){
					//GET request
					int32_t len = strlen(ap_get_page);
					err_t err = netconn_write(newconn, ap_get_page, len, NETCONN_COPY);
					if (err != ERR_OK){
						printf("ap_get_page NOT sent\n");
					}
				}
				else if (rq[0] == 'P' && rq[1] == 'O' && rq[2] == 'S' && rq[3] == 'T'){
					//POST request
					nvs_handle storage_handle = 0;

					esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
					if (err != ESP_OK) {
						printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
					}
					else {
						char *c1, *c2;
						int len;
						char *ssid = NULL, *pass = NULL, *mdns_host = NULL;

						//get ssid from response
						c1 = strstr(rq, "ssid=");
						c2 = strchr(c1, '&');
						if ((c1 != NULL) && (c2 != NULL)){
							len = c2 - c1 - 5;
							if (len > 0){
								ssid = malloc(len + 1);
								memset(ssid, 0, len + 1);
								memcpy(ssid, c1 + 5, len);
								printf("ssid: %s\n", ssid);
								ESP_ERROR_CHECK(nvs_set_str(storage_handle, "ssid", ssid));
								counter++;
							}
							else{
								printf("ssid not defined\n");
							}
						}
						else{
							printf("ssid not found\n");
						}

						//get password from response
						c1 = strstr(rq, "pass=");
						c2 = strchr(c1, '&');
						if ((c1 != NULL) && (c2 != NULL)){
							len = c2 - c1 - 5;
							if (len > 0){
								pass = malloc(len + 1);
								memset(pass, 0, len + 1);
								memcpy(pass, c1 + 5, len);
								printf("pass: %s\n", pass);
								ESP_ERROR_CHECK(nvs_set_str(storage_handle, "pass", pass));
								counter++;
							}
							else{
								printf("Password not defined\n");
							}
						}
						else{
							printf("Password not found\n");
						}

						//get mdns hostname from response
						c1 = strstr(rq, "mdns_host=");
						if (c1 != NULL){
							len = tcp_len - ((c1 + 10) - rq);
							if (len > 0){
								mdns_host = malloc(len + 1);
								memset(mdns_host, 0, len + 1);
								memcpy(mdns_host, c1 + 10, len);
								printf("mdns hostname: %s\n", mdns_host);
								ESP_ERROR_CHECK(nvs_set_str(storage_handle, "mdns_host", mdns_host));
								counter++;
							}
							else{
								printf("node name not defined\n");
							}
						}
						else{
							printf("node name not found\n");
						}

						printf("Committing updates in NVS ... ");
						esp_err_t err = nvs_commit(storage_handle);
						printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
						// Close NVS
						nvs_close(storage_handle);

						free(ssid);
						free(pass);
						free(mdns_host);
					}
					netconn_write(newconn, ap_res_header_ok, strlen(ap_res_header_ok), NETCONN_COPY);
				}
				else{
					printf("request out of service\n%s\n", rq);
				}
			}
			else{
				//connection is closed
				if (net_err == ERR_CLSD){
					printf("TCP was closed by client\n");
				}
			}
			netbuf_free(inbuf);
			netbuf_delete(inbuf);

			netconn_close(newconn);
			if (counter == 3){
				fflush(stdout);
				esp_wifi_stop();
				esp_restart();
				//*node_restart = true;
			}
			else{
				counter = 0;
			}
		}//netconn_accept
	}
	vTaskDelete(NULL);
}
