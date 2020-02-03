/*
 * thing_rgb_led_line.c
 *
 *  Created on: Jan 27, 2020
 *      Author: kz
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "simple_web_thing_server.h"

#include "common.h"
#include "thing_rgb_led_line.h"
#include "rgb_color.h"
#include "ws2812b.h"
#include "patterns.h"

xSemaphoreHandle led_line_mux;
xSemaphoreHandle refresh_sem;
uint8_t *rgb_buff;
static pRefreshFun funTab[PATTERNS_NR];
pattParam_t *paramTab[PATTERNS_NR];
refreshParam_t led_line_param;
thing_t *led_line = NULL;

char *color_model_jsonize(property_t *p);
char *color_value_jsonize(property_t *p);

//set functions
int8_t diodes_set(char *new_value_str); //set number of diodes
int8_t pattern_set(char *new_value_str); //set number of pattern
int8_t speed_set(char *new_value_str); //set speed value
int8_t color_set(char *new_value_str); //set color for some patterns
int8_t brightness_set(char *new_value_str); //set brightness
int8_t on_off_set(char *new_value_str); //switch ON/OFF

//thing description
char patterns_name_tab[8][20] = {"Standby", "Running point", "Rgb palette",
								"Tetris", "Static", "Floating ends", "New Year",
								"Christmas"}; 
//char led_line_disc[] = "web connected color leds";
at_type_t led_line_type;

//------  property "on" - ON/OFF state
static uint8_t on_off_state;
static int8_t standby_counter = 0;
property_t *prop_on;
at_type_t on_prop_type;

//------  property "diodes" - number of diodes
int32_t diodes;
property_t *prop_diodes;
at_type_t diodes_prop_type;

//------  property "pattern" - current pattern
property_t *prop_pattern;
static int32_t old_pattern;
at_type_t pattern_prop_type;

enum_item_t enum_run_point, enum_rgb_palette, enum_tetris, enum_static;
enum_item_t enum_float_ends, enum_new_year, enum_christmas;

//-------- property color
static char color[] = "#00ff00"; //current color
property_t *prop_color;
at_type_t color_prop_type;

//-------- property speed
static int32_t speed; //0..100 in percent
property_t *prop_speed;
at_type_t speed_prop_type;

//-------- property brightness
static int32_t brightness; //0..100 in percent
property_t *prop_brgh;
at_type_t brgh_prop_type;

//SPI variables
//extern uint8_t *spi_buff;
//Transaction descriptors. Declared static so they're not allocated on the stack;
//we need this memory even when this function is finished because the SPI driver
//needs access to it later.
//extern spi_transaction_t spi_trans;
//extern spi_device_handle_t spi_dev;

//task functions prototypes
void refreshRgb(void *arg);
void refreshTimer(void *arg);

void set_dt(pattParam_t *patt_param);

// SPI ---------------------------------------
//SPI hardware config
#define PIN_NUM_MOSI 23
static void spiInit(void);
//SPI variables
uint8_t *spi_buff;
uint32_t spi_buff_len = 0;
//Transaction descriptors. Declared static so they're not allocated on the stack;
//we need this memory even when this function is finished because the SPI driver
//needs access to it later.
static spi_transaction_t spi_trans;
static spi_device_handle_t spi_dev;
static void spiInit(void);

/* *****************************************************************
 *
 * switch led line ON or OFF
 *
 * *****************************************************************/
int8_t on_off_set(char *new_value_str){
	int8_t res = 1;

	if (strcmp(new_value_str, "true") == 0){
		on_off_state = ON;
		standby_counter = 5;
	}
	else{
		on_off_state = OFF;
		standby_counter = 0;
	}

	xSemaphoreGive(refresh_sem);
	//inform_all_subscribers(prop_on);

	return res;
}


/*****************************************************************
 *
 * set number of diodes
 * normally it will be used only by configuration of the device
 *
 * ***************************************************************/
int8_t diodes_set(char *new_value_str){
	int8_t res = 1;
	nvs_handle storage_handle = 0;

	int32_t d = atoi(new_value_str);

	if ((d >= LEDS_MIN) && (d < LEDS_MAX)){
		xSemaphoreTake(led_line_mux, portMAX_DELAY);
		diodes = d;
		
		//save new diodes into NVS memory
		esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
		if (err == ESP_OK){
			err = nvs_set_i32(storage_handle, "run_diodes", d);
			nvs_close(storage_handle);
		}
		
		xSemaphoreGive(led_line_mux);
		
		//force line refresh
		xSemaphoreGive(refresh_sem);
	}
	else{
		res = -1;
	}

	return res;
}


/*******************************************************
 *
 * convert color {0, 7, 255} into string "#0007ff"
 *
 * *****************************************************/
char *convert_color_into_web_str(rgb_t *c){
	char p[] = "#%02x%02x%02x";
	char *buff;

	buff = malloc(10);
	memset(buff, 0, 10);
	sprintf(buff, p, c -> red, c -> green, c -> blue);

	return buff;
}


/* ****************************************************************
 *
 * set number of pattern
 *
 * ****************************************************************/
int8_t pattern_set(char *new_value_str){
	int8_t res = -1;
	uint16_t p = 0;
	char *c1, *buff = NULL;//, *prev_patt;
	nvs_handle storage_handle = 0;
	
	//in websocket quotation mark is not removed
	//(in http should be the same but is not)
	buff = malloc(strlen(new_value_str));
	if (new_value_str[0] == '"'){
		memset(buff, 0, strlen(new_value_str));
		char *ptr = strchr(new_value_str + 1, '"');
		int len = ptr - new_value_str - 1;
		memcpy(buff, new_value_str + 1, len);
	}
	else{
		strcpy(buff, new_value_str);
	}
	
	//set pattern
	//prev_patt = (char *)prop_pattern -> value;
	if (prop_pattern -> enum_list != NULL){
		enum_item_t *enum_item = prop_pattern -> enum_list;
		while (enum_item != NULL){
			if (strcmp(buff, enum_item -> value.str_addr) == 0){
				prop_pattern -> value = enum_item -> value.str_addr;
				res = 1;
				break;
			}
			else{
				enum_item = enum_item -> next;
			}
			p++;
		}
	}

	if (p < PATTERNS_NR){
		p++;
		if (old_pattern != p){
			old_pattern = p;
			xSemaphoreTake(led_line_mux, portMAX_DELAY);

			led_line_param.runningPattern = p;
			brightness = paramTab[p] -> brightness;
			speed = (int32_t)paramTab[p] -> speed;
			if (speed == 0){
				speed = 1;
			}
			c1 = convert_color_into_web_str(&paramTab[p]-> color_1);
			memcpy(color, c1, 7);
			free(c1);
			
			//save new pattern into NVS memory
			esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
			if (err == ESP_OK){
				err = nvs_set_u16(storage_handle, "run_patt", p);
				nvs_close(storage_handle);
			}

			xSemaphoreGive(led_line_mux);

			//unblock refresher - it causes shorter step length after pattern change
			xSemaphoreGive(refresh_sem);

			//TODO: pack all data into one message
			inform_all_subscribers_prop(prop_color);
			inform_all_subscribers_prop(prop_speed);
			inform_all_subscribers_prop(prop_brgh);
		}
	}
	else{
		res = -1;
	}

	return res;
}


/* *****************************************************************
 *
 * set the speed of refreshing the leds
 * input value in percentages 0..100
 * "0" is special value, means "static color", refreshing is stopped
 *
 * *****************************************************************/
int8_t speed_set(char *new_value_str){
	int8_t res = 1;
	int16_t s;
	pattParam_t *patt_param;
	uint16_t i;
	nvs_handle storage_handle = 0;
	char buff[16];

	memset(buff, 0, 16);
	s = (uint16_t)atoi(new_value_str);
	xSemaphoreTake(led_line_mux, portMAX_DELAY);
	
	i = led_line_param.runningPattern;
	patt_param = paramTab[i];
	patt_param -> speed = s;
	set_dt(patt_param);
	speed = s;
	
	//save new speed into NVS memory
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
	if (err == ESP_OK){
		sprintf(buff, "p%i_speed", i);
		err = nvs_set_u16(storage_handle, buff, s);
		nvs_close(storage_handle);
	}

	xSemaphoreGive(led_line_mux);

	xSemaphoreGive(refresh_sem);

	return res;
}


/* ******************************************************************
 *
 * set color for some patterns
 *
 * ******************************************************************/
int8_t color_set(char *buff){
	int8_t res = 1;
	uint8_t red8, green8, blue8;
	char c[3];
	pattParam_t *patt_param;
	uint16_t i;
	nvs_handle storage_handle = 0;

	//printf("color: %s\n", buff);

	c[2] = 0;
	//RED
	c[0] = buff[2];
	c[1] = buff[3];
	red8 = (unsigned char)strtol(c, NULL, 16);
	//GREEN
	c[0] = buff[4];
	c[1] = buff[5];
	green8 = (unsigned char)strtol(c, NULL, 16);
	//BLUE
	c[0] = buff[6];
	c[1] = buff[7];
	blue8 = (unsigned char)strtol(c, NULL, 16);

	xSemaphoreTake(led_line_mux, portMAX_DELAY);

	memcpy(color, buff + 1, 7);
	i = led_line_param.runningPattern;
	patt_param = paramTab[i];
	//set color in current pattern
	patt_param -> color_1.red = red8;
	patt_param -> color_1.green = green8;
	patt_param -> color_1.blue = blue8;
	
	//save new color into NVS memory for pattern "static color"
	if (i == 4){
		esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
		if (err == ESP_OK){
			//sprintf(buff, "p%i_brgh", i);
			nvs_set_u8(storage_handle, "p4_red", red8);
			nvs_set_u8(storage_handle, "p4_green", green8);
			nvs_set_u8(storage_handle, "p4_blue", blue8);
			nvs_close(storage_handle);
		}
	}

	xSemaphoreGive(led_line_mux);

	xSemaphoreGive(refresh_sem);

	return res;
}


/* ****************************************************************
 *
 * set brightness
 *
 * ****************************************************************/
int8_t brightness_set(char *new_value_str){
	int32_t res = 1, brgh;
	pattParam_t *pattParam;
	char buff[16];
	uint16_t i;
	nvs_handle storage_handle = 0;

	memset(buff, 0, 16);
	brgh = atoi(new_value_str);
	if (brgh > 100){
		brgh = 100;
	}
	else if (brgh < 5){
		brgh = 5;
	}

	xSemaphoreTake(led_line_mux, portMAX_DELAY);

	i = led_line_param.runningPattern;
	pattParam = paramTab[i];
	pattParam -> brightness = brgh;
	brightness = brgh;
	
	//save new brightness into NVS memory
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
	if (err == ESP_OK){
		sprintf(buff, "p%i_brgh", i);
		err = nvs_set_u8(storage_handle, buff, brgh);
		nvs_close(storage_handle);
	}
	
	xSemaphoreGive(led_line_mux);
	
	//force line refresh
	xSemaphoreGive(refresh_sem);

	return res;
}


/****************************************************
 *
 * jsonize model of RGB color property
 *
 * **************************************************/
char *color_model_jsonize(property_t *p){
	char *buff;

	//only unit printed in model, is it enough?
	buff = malloc(12 + strlen(p -> unit));
	sprintf(buff, "\"unit\":\"%s\",", p -> unit);

	return buff;
}


/****************************************************
 *
 * RGB color jsonize
 *
 * **************************************************/
char *color_value_jsonize(property_t *p){
	char *buff;

	buff = malloc(20);
	buff[0] = '\"';
	buff[1] = 'c';
	buff[2] = 'o';
	buff[3] = 'l';
	buff[4] = 'o';
	buff[5] = 'r';
	buff[6] = '\"';
	buff[7] = ':';
	buff[8] = '\"';
	memcpy(&buff[9], p -> value, 7);
	buff[16] = '\"';
	buff[17] = 0;

	return buff;
}


/******************************************************************
 *
 * turn off all diodes
 *
 * *****************************************************************/
void clearAllDiodes(int32_t d){
	esp_err_t ret;
	uint32_t len;

	memset(rgb_buff, 0, d * 3);
	convertRgb2Bits(rgb_buff, spi_buff, d * 3);

	len = d * 9;
	if (len%4 != 0){
		len += 4 - len%4;
	}
	spi_trans.length = len * 8;
	spi_trans.rxlength = len * 8;
	ret = spi_device_transmit(spi_dev, &spi_trans);
	ESP_ERROR_CHECK(ret);
	vTaskDelay(10 / portTICK_PERIOD_MS);
}


//******************************************
//thread function for refreshing RGB value in buffer
void refreshRgb(void *arg){
	int32_t counter = 0, patt, oldDiodes;
	//pattParam_t *param;
	uint8_t refLeds, flags = 0;

    printf("RGB refresh task starting\n");
    oldDiodes = diodes;

    for(;;){
    	counter++;
    	//wait for refresh timer
    	xSemaphoreTake(refresh_sem, portMAX_DELAY);

    	xSemaphoreTake(led_line_mux, portMAX_DELAY);
    	//new diode number set by user
    	if (oldDiodes != diodes){
    		clearAllDiodes(oldDiodes);
    		oldDiodes = diodes;
    		//allocate new buffer for spi RGB data
    		free(spi_buff);
    		free(rgb_buff);

    		uint32_t spi_buff_len = diodes * 9;
    		if (spi_buff_len%4 != 0){
    			spi_buff_len += 4 - spi_buff_len%4;
    		}
    		spi_buff = heap_caps_malloc(spi_buff_len, MALLOC_CAP_DMA);
    		if (spi_buff == NULL){
    			printf("ERROR: spiBuff not allocated\n");
    		}
    		spi_trans.tx_buffer = spi_buff;
    		spi_trans.length = spi_buff_len * 8;
    		spi_trans.rxlength = spi_buff_len * 8;
    		rgb_buff = malloc(diodes * 3);
    		set_rgb_buff(&paramTab[0], rgb_buff);
    	}

    	//if line is OFF run stand-by pattern
    	if (on_off_state == ON){
    		patt = led_line_param.runningPattern;
    	}
    	else{
    		patt = 0;
    	}
    	//run pattern function
		refLeds = funTab[patt](flags);
		xSemaphoreGive(led_line_mux);

		if (refLeds > 0){
			esp_err_t ret;
			uint8_t b1, b2;

			//switch color position, for WS2812B should be GRB
			for (int i = 0; i < diodes; i++){
				b1 = rgb_buff[i * 3];
				b2 = rgb_buff[i * 3 + 1];
				rgb_buff[i * 3] = b2;
				rgb_buff[i * 3 + 1] = b1;
			}
			//convert RGB to SPI format
			convertRgb2Bits(rgb_buff, spi_buff, oldDiodes * 3);

			ret = spi_device_transmit(spi_dev, &spi_trans);
			ESP_ERROR_CHECK(ret);
		}
    }
}


// **********************************************
//
// thread function for refresher timer
//
// *********************************************/
void refreshTimer(void *arg){
   	int patt;
	int32_t delay;
	portTickType xLastWakeTime;

	printf("RefreshTimer task is starting\n");
	xLastWakeTime = xTaskGetTickCount();

    for(;;){
    	xSemaphoreTake(led_line_mux, portMAX_DELAY);
    	//read how long to wait for next wake up
    	if (on_off_state == ON){
    		//if line is OFF run stand-by pattern
    		patt = led_line_param.runningPattern;
    		delay = led_line_param.paramTab[patt] -> dt;
    	}
    	else{
    		patt = 0;
    		delay = led_line_param.paramTab[0] -> dt;
    	}
        xSemaphoreGive(led_line_mux);

        //start RGB refresh task
        if (delay > 0){
        	if (on_off_state == ON){
        		xSemaphoreGive(refresh_sem);
        	}
        	else{
        		if (standby_counter == 5){
        			standby_counter = 0;
        			xSemaphoreGive(refresh_sem);
        		}
        		else{
        			standby_counter++;
        		}
        	}
        	//delay = delay >> 10;
        	if (delay < 10){
        		//max refresh frequency = 100 Hz
        		delay = 10;
        	}
        	vTaskDelayUntil(&xLastWakeTime, (delay / portTICK_PERIOD_MS));
        }
        else{
        	//dt is "0" for speed zero - refreshing is stopped
        	//but every second check if new value is set
        	//(it could be done by stoping this thread by setting new speed!)
        	vTaskDelayUntil(&xLastWakeTime, (1000 / portTICK_PERIOD_MS));
        }
    }
}


/**/
void set_dt(pattParam_t *patt_param){
	double dt, freq_1, freq;
	uint16_t dfreq;
	
	dfreq = patt_param -> freq_max - patt_param -> freq_min;
	freq_1 = ((double)((patt_param -> speed - 1) * dfreq))/99.0;
	freq = (double)(patt_param -> freq_min) + freq_1;
	dt = 1000 / freq;
	patt_param -> dt = (int16_t)dt; //result in milliseconds
}


/**/
void init_nvs_parameters(){
	nvs_handle storage_handle = 0;
	esp_err_t err;
	uint8_t i;
	char buff[16];
	
	//read data written in NVS
	err = nvs_open("storage", NVS_READWRITE, &storage_handle);
	if (err != ESP_OK) {
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	}
	else {
		//get the last running pattern
		err = nvs_get_u16(storage_handle, "run_patt",
							&(led_line_param.runningPattern));
		if (err != ESP_OK){
			led_line_param.runningPattern = 1;
		}
		old_pattern = led_line_param.runningPattern;
		//printf("running patt: %i\n", led_line_param.runningPattern);
		
		//get pattern parameters-------------------------------
		for (i = 1; i < PATTERNS_NR; i++){
			memset(buff, 0, 16);
			//brightness
			sprintf(buff, "p%i_brgh", i);
			err = nvs_get_u8(storage_handle, buff, &(paramTab[i] -> brightness));
			//speed
			sprintf(buff, "p%i_speed", i);
			err = nvs_get_u16(storage_handle, buff, &(paramTab[i] -> speed));
			if (err == ESP_OK){
				set_dt(paramTab[i]);
			}
		}
		
		//read color for static pattern
		//color
		uint8_t red, green, blue;
		err = nvs_get_u8(storage_handle, "p4_red", &red);
		if (err == ESP_OK){
			paramTab[4] -> color_1.red = red;
		}			
		err = nvs_get_u8(storage_handle, "p4_green", &green);
		if (err == ESP_OK){
			paramTab[4] -> color_1.green = green;
		}
		err = nvs_get_u8(storage_handle, "p4_blue", &blue);
		if (err == ESP_OK){
			paramTab[4] -> color_1.blue = blue;
		}
	
		nvs_close(storage_handle);
	}
}


/*****************************************************************
 *
 * initialize led line thing with all properties
 *
 * ***************************************************************/
thing_t *init_rgb_led_line(){
	nvs_handle storage_handle = 0;
	esp_err_t err;

	led_line_param.funTab = funTab;
	led_line_param.paramTab = paramTab;
	
	err = nvs_open("storage", NVS_READWRITE, &storage_handle);
	if (err != ESP_OK) {
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	}
	else {
		//get the number of diodes set
		err = nvs_get_i32(storage_handle, "run_diodes", &diodes);
		if (err != ESP_OK){
			diodes = LEDS_MIN;
		}
		printf("diodes: %i\n", diodes);
		rgb_buff = malloc(diodes * 3);
		memset(rgb_buff, 0, diodes * 3);
		nvs_close(storage_handle);
	}
	initRgbPatterns(&diodes, rgb_buff, &paramTab[0], &funTab[0]);
	init_nvs_parameters();
	
	spiInit();
	
	clearAllDiodes(diodes);
	
	speed = paramTab[led_line_param.runningPattern] -> speed;
	brightness = paramTab[led_line_param.runningPattern] -> brightness;
	on_off_state = OFF;

	vSemaphoreCreateBinary(refresh_sem);
	led_line_mux = xSemaphoreCreateMutex();

	//start tasks for refreshing led values
	xTaskCreate(refreshRgb, "led_refresher", 2048*4, NULL, 2, NULL);
	//timer for refresher
	xTaskCreate(refreshTimer, "led_line_timer", 1024, NULL, 1, NULL);

	//initialize thing and it's properties
	led_line = thing_init();
	led_line -> id = "RGB line";
	led_line -> at_context = things_context;
	led_line -> model_len = 1800;
	//set @type
	led_line_type.at_type = "Light";
	led_line_type.next = NULL;
	set_thing_type(led_line, &led_line_type);
	led_line -> description = "web connected color leds";

	//property: ON/OFF
	prop_on = property_init(NULL, NULL);
	prop_on -> id = "on";
	prop_on -> description = "ON/OFF";
	on_prop_type.at_type = "OnOffProperty";
	on_prop_type.next = NULL;
	prop_on -> at_type = &on_prop_type;
	prop_on -> type = VAL_BOOLEAN;
	prop_on -> value = &on_off_state;
	prop_on -> title = "ON/OFF";
	prop_on -> read_only = false;
	prop_on -> set = on_off_set;
	prop_on -> mux = led_line_mux;
	add_property(led_line, prop_on); //add property to thing

	//property: diodes
	prop_diodes = property_init(NULL, NULL);
	prop_diodes -> id = "diodes";
	prop_diodes -> description = "number of controlled diodes";
	diodes_prop_type.at_type = "LevelProperty";
	diodes_prop_type.next = NULL;
	prop_diodes -> at_type = &diodes_prop_type;
	prop_diodes -> type = VAL_INTEGER;
	prop_diodes -> value = &diodes;
	prop_diodes -> max_value.int_val = LEDS_MAX;
	prop_diodes -> min_value.int_val = 3;
	prop_diodes -> unit = "pcs";
	prop_diodes -> title = "diodes";
	prop_diodes -> read_only = false;
	prop_diodes -> set = diodes_set;
	prop_diodes -> mux = led_line_mux;
	add_property(led_line, prop_diodes); //add property to thing

	//property: pattern
	prop_pattern = property_init(NULL, NULL);
	prop_pattern -> id = "pattern";
	prop_pattern -> description = "current color pattern";
	pattern_prop_type.at_type = "PatternProperty";
	pattern_prop_type.next = NULL;
	prop_pattern -> at_type = &pattern_prop_type;
	prop_pattern -> type = VAL_STRING;
	
	prop_pattern -> enum_prop = true;
	prop_pattern -> enum_list = &enum_run_point;
	enum_run_point.value.str_addr = patterns_name_tab[1];
	enum_run_point.next = &enum_rgb_palette;
	enum_rgb_palette.value.str_addr = patterns_name_tab[2];
	enum_rgb_palette.next = &enum_tetris;
	enum_tetris.value.str_addr = patterns_name_tab[3];
	enum_tetris.next = &enum_static;
	enum_static.value.str_addr = patterns_name_tab[4];
	enum_static.next = &enum_float_ends;
	enum_float_ends.value.str_addr = patterns_name_tab[5];
	enum_float_ends.next = &enum_new_year;
	enum_new_year.value.str_addr = patterns_name_tab[6];
	enum_new_year.next = &enum_christmas;
	enum_christmas.value.str_addr = patterns_name_tab[7];
	enum_christmas.next = NULL;
	
	prop_pattern -> value = patterns_name_tab[led_line_param.runningPattern];
	prop_pattern -> title = "pattern";
	prop_pattern -> read_only = false;
	prop_pattern -> set = pattern_set;
	prop_pattern -> mux = led_line_mux;
	add_property(led_line, prop_pattern);

	//property: color
	prop_color = property_init(NULL, NULL);
	prop_color -> id = "color";
	prop_color -> description = "color set for some patterns";
	color_prop_type.at_type = "ColorProperty";
	color_prop_type.next = NULL;
	prop_color -> at_type = &color_prop_type;
	prop_color -> type = VAL_STRING;
	prop_color -> value = &color;
	prop_color -> max_value.int_val = 0xffffff;
	prop_color -> min_value.int_val = 0;
	prop_color -> unit = "color RGB";
	prop_color -> title = "color";
	prop_color -> read_only = false;
	prop_color -> set = color_set;
	prop_color -> model_jsonize = color_model_jsonize;
	prop_color -> value_jsonize = color_value_jsonize;
	prop_color -> mux = led_line_mux;
	add_property(led_line, prop_color);

	//property: speed
	prop_speed = property_init(NULL, NULL);
	prop_speed -> id = "speed";
	prop_speed -> description = "led refreshment speed";
	speed_prop_type.at_type = "LevelProperty";
	speed_prop_type.next = NULL;
	prop_speed -> at_type = &speed_prop_type;
	prop_speed -> type = VAL_INTEGER;
	prop_speed -> value = &speed;
	prop_speed -> max_value.int_val = 100;
	prop_speed -> min_value.int_val = 1;
	prop_speed -> unit = "percent";
	prop_speed -> title = "speed";
	prop_speed -> read_only = false;
	prop_speed -> set = speed_set;
	prop_speed -> mux = led_line_mux;
	add_property(led_line, prop_speed);

	//property: brightness
	prop_brgh = property_init(NULL, NULL);
	prop_brgh -> id = "brgh";
	prop_brgh -> description = "led line brightness";
	brgh_prop_type.at_type = "BrightnessProperty";
	brgh_prop_type.next = NULL;
	prop_brgh -> at_type = &brgh_prop_type;
	prop_brgh -> type = VAL_INTEGER;
	prop_brgh -> value = &brightness;
	prop_brgh -> max_value.int_val = 100;
	prop_brgh -> min_value.int_val = 0;
	prop_brgh -> unit = "percent";
	prop_brgh -> title = "brightness";
	prop_brgh -> read_only = false;
	prop_brgh -> set = brightness_set;
	prop_brgh -> mux = led_line_mux;
	add_property(led_line, prop_brgh);

	return led_line;
}


/* *********************************************************************
 *
 * initialize SPI peripheral for sending LED WS2818 data
 *
 * *********************************************************************/
static void spiInit(void){
	esp_err_t ret;
	spi_bus_config_t buscfg = {
			.miso_io_num = -1,
	        .mosi_io_num = CONFIG_PIN_NUM_MOSI,
	        .sclk_io_num = -1,
	        .quadwp_io_num = -1,
	        .quadhd_io_num = -1,
			.max_transfer_sz = 0
	};
	spi_device_interface_config_t devcfg={
	        .clock_speed_hz = 800000*3,     //SPI clock
	        .mode = 0,                      //SPI mode 0
	        .spics_io_num = -1,         	//CS pin
	        .queue_size = 1,                //We want to be able to queue N transactions at a time
	        .pre_cb = NULL,  				//Specify pre-transfer callback to handle D/C line
			.post_cb = NULL					//spiAfterCallback,
	};

	//Initialize the SPI bus
	ret = spi_bus_initialize(VSPI_HOST, &buscfg, 1);
	ESP_ERROR_CHECK(ret);
	//Attach the LED strip to the SPI bus
	ret = spi_bus_add_device(VSPI_HOST, &devcfg, &spi_dev);
	ESP_ERROR_CHECK(ret);

	spi_buff_len = diodes * 9; //at start it is space for 5 diodes
	if (spi_buff_len%4 != 0){
		spi_buff_len += 4 - spi_buff_len%4;
	}
	spi_buff = heap_caps_malloc(spi_buff_len, MALLOC_CAP_DMA);
	if (spi_buff == NULL){
		printf("ERROR: spiBuff not allocated\n");
	}
	memset(&spi_trans, 0, sizeof(spi_transaction_t));
	spi_trans.length = spi_buff_len * 8;
	spi_trans.rxlength = spi_buff_len * 8;
	spi_trans.user = (void*)0;
	spi_trans.tx_buffer = spi_buff;
	spi_trans.rx_buffer = NULL;
}
