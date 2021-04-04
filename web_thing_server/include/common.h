/*
 * common.h
 *
 *  Created on: Jun 02, 2019
 *  Last edit on: Feb 19, 2021
 *      Author: Krzysztof Zurek
 *      krzzurek@gmail.com
 */

#ifndef COMMON_H_
#define COMMON_H_

#include "freertos/FreeRTOS.h"
#include "lwip/api.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#define MAX_OPEN_CONN 10	//max number of open connections

#define OFF 0
#define ON 1
#define ACTION_PROP_LEN 200
#define MAX_ACTION_REQUESTS 5

typedef enum {
	CONN_STATE_UNKNOWN = 0,
	CONN_HTTP_CLOSE = 1,
	CONN_HTTP_KEEP_ALIVE = 2,
	CONN_HTTP_RUNNING = 3,
	CONN_HTTP_STOPPED = 4,
	CONN_WS_RUNNING = 5,
	CONN_WS_CLOSE = 6
}CONN_STATE;

typedef enum {
	VAL_NULL,
	VAL_BOOLEAN,
	VAL_OBJECT,
	VAL_ARRAY,
	VAL_NUMBER,
	VAL_INTEGER,
	VAL_STRING
}VAL_TYPE;

typedef enum {
	ACT_PENDING,
	ACT_COMPLETED,
	ACT_EXECUTED,
	ACT_FAILED,
	ACT_CREATED,
	ACT_DELETED
}ACTION_STATUS;

// connection run state
typedef enum {
	CONN_DELETED = -1,
	CONN_STOP = 0,
	CONN_RUN = 1
} CONN_RUNING;

typedef enum {
	PROPERTY,
	ACTION,
	EVENT,
	UNKNOWN
}RESOURCE_TYPE;

typedef enum{
	CONN_UNKNOWN = 0,
	CONN_HTTP = 1,
	CONN_WS = 2
} CONN_TYPE; //connection type

typedef struct thing_t thing_t;
typedef struct property_t property_t;
typedef struct at_type_t at_type_t;
typedef struct enum_item_t enum_item_t;

typedef union {
	int32_t int_val;
	float float_val;
} int_float_u;

struct at_type_t{
	char *at_type;
	at_type_t *next;
};

union enum_value_t{
	bool bool_val;
	int int_val;
	float float_val;
	char *str_addr;
};

struct enum_item_t{
	union enum_value_t value;
	bool current;
	enum_item_t *next;
};

typedef struct{
	int8_t				index;
	CONN_TYPE			type;
	struct netconn 		*netconn_ptr;
	xTaskHandle 		task_handl;
	TimerHandle_t 		timer;
	uint32_t			ws_pings;
	uint32_t			ws_pongs;
	uint8_t				ws_close_initiator;
	uint16_t			ws_status_code;
	uint8_t				ws_state;
	uint32_t			packets;
	uint32_t			bytes;
	uint32_t			send_errors;
	int8_t				msg_to_send;
	thing_t				*thing;
	CONN_STATE			connection;
	uint32_t			requests;
	xSemaphoreHandle	mutex;
} connection_desc_t;

#endif /* COMMON_H_ */
