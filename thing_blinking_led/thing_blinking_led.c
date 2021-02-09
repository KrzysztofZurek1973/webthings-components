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
static TimerHandle_t constant_on_timer = NULL;

thing_t *blinking_led = NULL;
property_t *prop_led_on = NULL, *prop_led_freq = NULL;
action_t *constant_on = NULL;
action_input_prop_t *led_on_duration;
double constant_on_duration_min = 1; //seconds
double constant_on_duration_max = 600;
at_type_t blinking_led_type, prop_led_on_type, prop_led_freq_type;
at_type_t constant_on_input_attype;
action_input_prop_t *constant_on_duration;

static bool led_is_on = true;
static bool led_blinking = true;
static int dt_counter = 0;
static int dt_max = 50;
static int led_state = 0;
static int led_freq = 20; //Hz x 10

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
void constant_on_timer_fun(TimerHandle_t xTimer){
	
	complete_action(1, "constant_on", ACT_COMPLETED);
	
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
 * constant_on action
 * inputs:
 * 		- seconds of turn ON in json format, e.g.: "duration":10
 *
 * *******************************************************/
int8_t constant_on_run(char *inputs){
	int duration = 0, len;
	char *p1, buff[6];

	if (led_blinking == true){
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
		constant_on_timer = xTimerCreate("constant_on_timer",
									pdMS_TO_TICKS(duration * 1000),
									pdFALSE,
									pdFALSE,
									constant_on_timer_fun);
		xSemaphoreGive(led_mux);
		
		if (xTimerStart(constant_on_timer, 5) == pdFAIL){
			printf("action timer failed\n");
		}
	}

	return 0;

	inputs_error:
	printf("constant ON ERROR\n");
	return -1;
}


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
	blinking_led -> model_len = 1500;
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
	
	//create action "led_on", turn on led for specified seconds
	constant_on = action_init();
	constant_on -> id = "constant_on";
	constant_on -> title = "Constant ON";
	constant_on -> description = "Set led ON for some seconds";
	constant_on -> run = constant_on_run;
	constant_on_input_attype.at_type = "ToggleAction";
	constant_on_input_attype.next = NULL;
	constant_on -> input_at_type = &constant_on_input_attype;
	constant_on_duration = action_input_prop_init("duration",
							VAL_INTEGER, true,
							&constant_on_duration_min,
							&constant_on_duration_max,
							"seconds");
	add_action_input_prop(constant_on, constant_on_duration);
	
	add_action(blinking_led, constant_on);
	
	xTaskCreate(&blinking_led_fun, "blinking led", configMINIMAL_STACK_SIZE * 4,
				NULL, 5, &blinking_led_task);

	return blinking_led;
}
