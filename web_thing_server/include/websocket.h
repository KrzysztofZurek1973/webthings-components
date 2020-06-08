/*
 * websocket.h
 *
 *  Created on: Jul 1, 2019
 *      Author: kz
 */

#ifndef MAIN_WEBSOCKET_H_
#define MAIN_WEBSOCKET_H_

#include <stdint.h>

#include "lwip/api.h"
#include "freertos/queue.h"
#include "common.h"

typedef void *ws_handler_t;

/** \brief Opcode according to RFC 6455*/
typedef enum {
	WS_OP_CON = 0x0, 				/*!< Continuation Frame*/
	WS_OP_TXT = 0x1, 				/*!< Text Frame*/
	WS_OP_BIN = 0x2, 				/*!< Binary Frame*/
	WS_OP_CLS = 0x8, 				/*!< Connection Close Frame*/
	WS_OP_PIN = 0x9, 				/*!< Ping Frame*/
	WS_OP_PON = 0xa 				/*!< Pong Frame*/
} WS_OPCODES;

typedef struct{
	WS_OPCODES opcode:4;
	uint8_t reserved:3;
	uint8_t fin:1;
	uint8_t payload_len:7;
	uint8_t mask:1;
}ws_frame_header_t;

typedef union{
	ws_frame_header_t h;
	uint8_t bytes[2];
} ws_frame_header_u_t;

// state of websocket
typedef enum {
	WS_CLOSED =	0x0,
	WS_OPEN = 0x1,
	WS_CLOSING = 0x2,
	WS_OPENING = 0x3
} WS_STATE;

// ws close handshake initiator
typedef enum {
	WS_CLOSE_BY_NOBODY = 0,
	WS_CLOSE_BY_SERVER = 1,
	WS_CLOSE_BY_CLIENT = 2
} WS_CLOSE_INITIATOR;

typedef enum {
	NORMAL_CLOSE	= 1000,
	GOING_AWAY		= 1001,
	PROTOCOL_ERR	= 1002,
	INCORRECT_DATA	= 1003,
	ABNORMAL_CLS	= 1006,
	DATA_INCONSIST	= 1007,
	POLICY_ERR		= 1008,
	DATA_TO_BIG		= 1009,
	SERVER_ERR		= 1011
} WS_STATUS_CODE;

typedef struct{
	uint8_t *payload;
	uint16_t len;
	connection_desc_t *conn_desc;
	WS_OPCODES opcode:4;
	uint8_t ws_frame:1; //ws - 1, non ws - 0
	uint8_t text:1; //1 - text frame, 0 - binary frame
}ws_queue_item_t;

typedef struct{
	uint8_t *payload;
	int len;
}ws_send_data;

int8_t ws_server_init(uint16_t port);
int8_t ws_server_stop(void);
int8_t ws_send(ws_queue_item_t *item, int32_t wait_ms);
int8_t ws_receive(char *rq, uint16_t tcp_len, connection_desc_t *conn_desc);
xQueueHandle ws_get_recv_queue(void);

#endif /* MAIN_WEBSOCKET_H_ */
