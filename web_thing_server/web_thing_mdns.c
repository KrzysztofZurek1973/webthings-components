/*
 * web_thing_mdns.c
 *
 *  Created on: Oct 3, 2019
 *      Author: kz
 */

#include "esp_system.h"
//#include "esp_wifi.h"
#include "mdns.h"
#include "esp_log.h"

#include "web_thing_mdns.h"

//mDNS
#define MDNS_INSTANCE "Mozilla IoT node"
#define MDNS_HOSTNAME "iot-node-ap"

static const char *TAG_MDNS = "mdns";
static const char *TAG_MDNS_AP = "wifi softAP";

// *********************************************
//mDNS initialization
void initialize_mdns(char *_hostname, bool ap, uint16_t port){
    char *hostname, port_buff[6];

    if ((_hostname == NULL) || (ap == true)){
    	hostname = MDNS_HOSTNAME;
    }
    else{
    	hostname = _hostname;
    }

    //initialize mDNS
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(hostname) );
    if (ap == true){
    	ESP_LOGI(TAG_MDNS_AP, "mdns hostname set to: [%s]", hostname);
    }
    else{
    	ESP_LOGI(TAG_MDNS, "mdns hostname set to: [%s]", hostname);
    }
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set(MDNS_INSTANCE) );

    //structure with TXT records
    itoa(port, port_buff, 10);
    mdns_txt_item_t serviceTxtData[2] = {
        {"port",port_buff},
        {"path","/"}
    };

    //initialize service
    ESP_ERROR_CHECK(
    	mdns_service_add("webthing service", "_webthing", "_tcp", port, serviceTxtData, 2)
    );
}
