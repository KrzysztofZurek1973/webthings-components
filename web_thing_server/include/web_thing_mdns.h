/*
 * web_thing_mdns.h
 *
 *  Created on: Oct 3, 2019
 *      Author: Krzysztof Zurek
 */

#ifndef MAIN_WEB_THING_MDNS_H_
#define MAIN_WEB_THING_MDNS_H_

#define MDNS_DOMAIN "local"

void initialize_mdns(char *_hostname, bool ap, uint16_t port);

#endif /* MAIN_WEB_THING_MDNS_H_ */
