/*
 * html_parser.h
 *
 *  Created on: June 18, 2019
 *      Author: Krzysztof Zurek
 */

#ifndef HTTP_PARSER_H_
#define HTTP_PARSER_H_

#include <stdint.h>
#include "lwip/api.h"

#include "web_thing.h"
#include "common.h"

uint8_t http_receive(char *rq, uint16_t tcp_len, connection_desc_t *conn_desc);

#endif /* HTTP_PARSER_H_ */
