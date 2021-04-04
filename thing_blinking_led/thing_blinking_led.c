/*
 * thing_button.c
 *
 *  Created on: Jan 2, 2020
 *      Author: Krzysztof Zurek
 *		e-mail: krzzurek@gmail.com
 */
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "simple_web_thing_server.h"
#include "thing_blinking_led.h"

//button GPIO
#define GPIO_LED			(CONFIG_LED_GPIO)
#define GPIO_LED_MASK		(1ULL << GPIO_LED)

xSemaphoreHandle DRAM_ATTR led_mux;
xTaskHandle blinking_led_task; //blinking led RTOS task
static TimerHandle_t action_timer = NULL;

thing_t *blinking_led = NULL;
property_t *prop_led_on = NULL, *prop_led_freq = NULL;
action_t *action_settings;
at_type_t settings_attype;
int16_t action_run(char *inputs);
at_type_t blinking_led_type, prop_led_on_type, prop_led_freq_type;
action_input_prop_t *input_timer, *input_mode, *input_pattern, *input_sensor;

static bool led_is_on = true;
static bool led_blinking = true;
static int dt_counter = 0;
static int dt_max = 50;
static int led_state = 0;
static int led_freq = 20; //Hz x 10

enum_item_t enum_mode_rgb, enum_mode_rgb_white, enum_mode_white;
enum_item_t patt_1, patt_2, patt_3, patt_4;


/* ------ TEST ---------- */
/*
void set_led_pin(void){
	int pin_val;
	
	pin_val = gpio_get_level(GPIO_LED);
	pin_val ^= 1;
	gpio_set_level(GPIO_LED, pin_val);
}
*/


/****************************************************
*
* blinking led thread function
*
****************************************************/
void blinking_led_fun(void *param){
	dt_counter = dt_max - 1;
	
	for(;;){
		if (led_is_on == true){
			dt_counter++;
			if (dt_counter >= dt_max){
				dt_counter = 0;
				if (led_blinking == true){
					xSemaphoreTake(led_mux, portMAX_DELAY);
					//led_state = gpio_get_level(GPIO_LED);
					//led_state ^= 1;
					//gpio_set_level(GPIO_LED, led_state);
					
					if (led_state == 0){
						gpio_set_level(GPIO_LED, 1);
						led_state = 1;
					}
					else{
						gpio_set_level(GPIO_LED, 0);
						led_state = 0;
					}
					
					xSemaphoreGive(led_mux);
				}
			}
		}
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}


/*******************************************************************
*
* initialize blinking led
*
* ******************************************************************/
void init_led_io(void){
	gpio_config_t io_conf;

	//interrupt on both edges
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//bit mask of the pins, use GPIO4/5 here
	io_conf.pin_bit_mask = GPIO_LED_MASK;
	//set as input mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 0;
	io_conf.pull_down_en = 0;
	gpio_config(&io_conf);
}


/*******************************************************************
*
* set ON/OFF state
*
*******************************************************************/
int16_t led_set_on_off(char *new_value_str){
	int8_t res = 1;
	
	xSemaphoreTake(led_mux, portMAX_DELAY);
	if (strcmp(new_value_str, "true") == 0){
		led_is_on = true;
		dt_counter = 0;
		gpio_set_level(GPIO_LED, 1);
		led_state = 1;
	}
	else{
		led_is_on = false;
		gpio_set_level(GPIO_LED, 0);
		led_state = 0;
	}

	xSemaphoreGive(led_mux);
	
	return res;
}


/*******************************************************************
*
* set new blinking frequency
* int values frequency x 10
*
*******************************************************************/
int16_t led_set_frequency(char *new_value_str){
	int8_t res = 0;
	int prev_freq, new_freq, fmin, fmax;
	
	prev_freq = led_freq;
	new_freq = atoi(new_value_str);
	fmin = prop_led_freq -> min_value.int_val;
	fmax = prop_led_freq -> max_value.int_val;
	if (new_freq != prev_freq){
		if ((new_freq >= fmin) && (new_freq <= fmax)){
			int new_dt_max = (int)(1000/new_freq);
			xSemaphoreTake(prop_led_freq -> mux, portMAX_DELAY);
			led_freq = new_freq;
			dt_max = new_dt_max;
			xSemaphoreGive(prop_led_freq -> mux);
			res = 1;
		}
		else{
			res = -1;
		}
	}
	
	return res;
}


/******************************************************
 *
 * Stop constant ON and back to blinking
 *
 * *****************************************************/
void action_timer_fun(TimerHandle_t xTimer){
	
	printf("Timer fun: Complete action\n");
	
	complete_action(1, "settings", ACT_COMPLETED);
	
	xSemaphoreTake(led_mux, portMAX_DELAY);
	led_blinking = true;
	gpio_set_level(GPIO_LED, 0);
	led_state = 0;
	dt_counter = 0;
	xSemaphoreGive(led_mux);
	
	xTimerDelete(xTimer, 100);
}


/**********************************************************
 *
 * run action function
 * inputs:
 * 		- duration
 * 		- mode
 * 		- color
 *
 * *******************************************************/

int16_t action_run(char *inputs){
	int duration = 0, len, mode = 0;
	int16_t inputs_exec = -1;
	char *p1, *p2, buff[20];

	printf("Run inputs: %s\n", inputs);

	//get duration ------------------------------------------------
	p1 = strstr(inputs, "timer");
	if (p1 == NULL) goto timer_run_mode;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_mode;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1);
	}
	else{
		len = p2 - p1 - 1;
	}
	memset(buff, 0, 20);
	memcpy(buff, p1 + 1, len);
	duration = atoi(buff);
	if ((duration < 600) || (duration > 0)){
		inputs_exec++;
		printf("timer: %i\n", duration);
	}
	else{
		goto timer_run_mode;
	}

	//get mode --------------------------------------------------------
timer_run_mode:
	p1 = strstr(inputs, "mode");
	if (p1 == NULL) goto timer_run_sensor;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_sensor;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1);
	}
	else{
		len = p2 - p1 - 1;
	}
	if (strstr(p1 + 1, "COLOR+WHITE") != NULL){
		mode = 0;
	}
	else if (strstr(p1 + 1, "COLOR") != NULL){
		mode = 1;
	}
	else if (strstr(p1 + 1, "WHITE") != NULL){
		mode = 2;
	}
	else{
		goto timer_run_sensor;
	}
	inputs_exec++;
	printf("mode: %i\n", mode);
	
	//get sensor active --------------------------------------------------------
timer_run_sensor:
	p1 = strstr(inputs, "sensor");
	if (p1 == NULL) goto timer_run_pattern;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_pattern;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1);
	}
	else{
		len = p2 - p1 - 1;
	}
	memset(buff, 0, 20);
	memcpy(buff, p1 + 1, len);
	bool sensor_active = false;
	p1 = strstr(buff, "true");
	if (p1 != NULL){
		sensor_active = true;
	}
	
	if (sensor_active == true){
		printf("sensor ACTIVE\n");
	}
	else{
		printf("sensor NOT ACTIVE\n");
	}
	
	inputs_exec++;

	//get color ---------------------------------------------------
timer_run_pattern:
	p1 = strstr(inputs, "pattern");
	if (p1 == NULL) goto timer_run_end;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_end;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1) - 2;
	}
	else{
		len = p2 - p1 - 3;
	}
	if (len > 19){
		len = 19;
	}
	memset(buff, 0, 20);
	memcpy(buff, p1 + 2, len);
	inputs_exec++;
	printf("pattern: %s\n", buff);

timer_run_end:
	if (inputs_exec >= 0){
		//start timer
		action_timer = xTimerCreate("action_timer",
									pdMS_TO_TICKS(1000),
									pdFALSE,
									pdFALSE,
									action_timer_fun);
		//xSemaphoreGive(led_mux);
		
		if (xTimerStart(action_timer, 5) == pdFAIL){
			printf("action timer failed\n");
		}
	}
	printf("run return: %i\n", inputs_exec);
	
	return inputs_exec;
}


/**********************************************************
 *
 * constant_on action
 * inputs:
 * 		- seconds of turn ON in json format, e.g.: "duration":10
 *
 * *******************************************************/
 /*
int16_t constant_on_run(char *inputs){
	int duration = 0, len;
	char *p1, buff[6];

	printf("Run inputs:\n%s\n", inputs);

	//if (led_blinking == true){
		//get duration value
		p1 = strstr(inputs, "duration");
		if (p1 == NULL){
			goto inputs_error;
		}
		p1 = strchr(p1, ':');
		if (p1 == NULL){
			goto inputs_error;
		}
		len = strlen(inputs) - (p1 + 1 - inputs);
		if (len > 5){
			goto inputs_error;
		}
		memset(buff, 0, 6);
		memcpy(buff, p1 + 1, len);
		duration = atoi(buff);
		if ((duration > 600) || (duration == 0)){
			goto inputs_error;
		}
		
		xSemaphoreTake(led_mux, portMAX_DELAY);
		gpio_set_level(GPIO_LED, 1);
		led_blinking = false;
		
		//start timer
		action_timer = xTimerCreate("on_timer",
									pdMS_TO_TICKS(duration * 1000),
									pdFALSE,
									pdFALSE,
									action_timer_fun);
		xSemaphoreGive(led_mux);
		
		if (xTimerStart(action_timer, 5) == pdFAIL){
			printf("action timer failed\n");
		}
	//}
	

	return 0;

	inputs_error:
	printf("constant ON ERROR\n");
	return -1;
}
*/


/*****************************************************************
 *
 * Initialization termostat thing and all it's properties
 *
 * ****************************************************************/
thing_t *init_blinking_led(void){
	
	led_mux = xSemaphoreCreateMutex();
	init_led_io();
	
	//create led thing
	blinking_led = thing_init();
	
	blinking_led -> id = "Led";
	blinking_led -> at_context = things_context;
	blinking_led -> model_len = 3000;
	//set @type
	blinking_led_type.at_type = "Light";
	blinking_led_type.next = NULL;
	set_thing_type(blinking_led, &blinking_led_type);
	blinking_led -> description = "Internet connected blinking LED";
	
	//create ON/OFF property
	prop_led_on = property_init(NULL, NULL);
	prop_led_on -> id = "led_on";
	prop_led_on -> description = "led ON/OFF state";
	prop_led_on_type.at_type = "OnOffProperty";
	prop_led_on_type.next = NULL;
	prop_led_on -> at_type = &prop_led_on_type;
	prop_led_on -> type = VAL_BOOLEAN;
	prop_led_on -> value = &led_is_on;
	prop_led_on -> title = "ON/OFF";
	prop_led_on -> read_only = false;
	prop_led_on -> set = led_set_on_off;
	prop_led_on -> mux = led_mux;

	add_property(blinking_led, prop_led_on); //add property to thing
	
	//create "frequency" property
	dt_max = (int)(1000/led_freq);
	prop_led_freq = property_init(NULL, NULL);
	prop_led_freq -> id = "frequency";
	prop_led_freq -> description = "led blinking frequency";
	prop_led_freq_type.at_type = "LevelProperty";
	prop_led_freq_type.next = NULL;
	prop_led_freq -> at_type = &prop_led_freq_type;
	prop_led_freq -> type = VAL_INTEGER;
	prop_led_freq -> value = &led_freq;
	prop_led_freq -> max_value.int_val = 300;
	prop_led_freq -> min_value.int_val = 1;
	prop_led_freq -> unit = "hertz";
	prop_led_freq -> title = "Frequency x 10";
	prop_led_freq -> read_only = false;
	prop_led_freq -> set = led_set_frequency;
	prop_led_freq -> mux = led_mux;

	add_property(blinking_led, prop_led_freq); //add property to thing
	
	//---------------------------------------------
	//create action "settings"
	action_settings = action_init();
	action_settings -> id = "settings";
	action_settings -> title = "SETTINGS";
	action_settings -> description = "More settings";
	action_settings -> run = action_run;
	settings_attype.at_type = "ToggleAction";
	settings_attype.next = NULL;
	action_settings -> input_at_type = &settings_attype;
	
	/*define action input properties
	*  input parameters:
	*	- type, e.g. VAL_INTEGER, VAL_STRING, VAL_BOOLEAN, VAL_NUMBER
	*	- true: required, false: not required
	*	- pointer to minimum value (type int_float_u)
	*	- pointer to maximum value (type int_float_u)
	*	- unit (as char array)
	*	- true: enum values
	*	- pointer to enum list (type enum_item_t)
	*/
	input_sensor = action_input_prop_init("sensor",
											VAL_BOOLEAN,
											false,
											NULL,
											NULL,
											NULL,
											false,
											NULL);
	add_action_input_prop(action_settings, input_sensor);
	
	int_float_u d_min, d_max;
	d_min.int_val = 0;
	d_max.int_val = 100;
	input_timer = action_input_prop_init("timer",
										VAL_INTEGER,//type
										false,		//required
										&d_min,		//min value
										&d_max,		//max value
										"minutes",	//unit
										false,		//is enum
										NULL);		//enum list pointer
	add_action_input_prop(action_settings, input_timer);

	//enums
	enum_mode_rgb.value.str_addr = "COLOR+WHITE";
	enum_mode_rgb.next = &enum_mode_rgb_white;
	enum_mode_rgb_white.value.str_addr = "COLOR";
	enum_mode_rgb_white.next = &enum_mode_white;
	enum_mode_white.value.str_addr = "WHITE";
	enum_mode_white.next = NULL;

	input_mode = action_input_prop_init("mode",
										VAL_STRING,
										false,
										NULL,
										NULL,
										NULL,
										true,
										&enum_mode_rgb);
	add_action_input_prop(action_settings, input_mode);

	//pattern names
	patt_1.value.str_addr = "STATIC";
	patt_1.next = &patt_2;
	patt_2.value.str_addr = "RGB";
	patt_2.next = &patt_3;
	patt_3.value.str_addr = "COLOR PULSE";
	patt_3.next = &patt_4;
	patt_4.value.str_addr = "RGB PULSE";
	patt_4.next = NULL;
	input_pattern = action_input_prop_init("pattern",
											VAL_STRING,
											false,
											NULL,
											NULL,
											NULL,
											true,
											&patt_1);
	add_action_input_prop(action_settings, input_pattern);

	add_action(blinking_led, action_settings);
	
	xTaskCreate(&blinking_led_fun, "blinking led", configMINIMAL_STACK_SIZE * 4,
				NULL, 5, &blinking_led_task);

	return blinking_led;
}
