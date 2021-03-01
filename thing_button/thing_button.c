/*
 * thing_button.c
 *
 *  Created on: Jan 1, 2020
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
#include "thing_button.h"

//button GPIO
#define GPIO_BUTTON		       	(CONFIG_BUTTON_GPIO)
#define GPIO_BUTTON_MASK		(1ULL << GPIO_BUTTON)

xSemaphoreHandle DRAM_ATTR button_sem;
xSemaphoreHandle DRAM_ATTR button_mux;
static int irq_counter = 0, irq_counter_1 = 0;
static bool DRAM_ATTR button_ready = false;

thing_t *iot_button = NULL;
property_t *prop_pushed, *prop_push_counter;
event_t *ten_times_event;
at_type_t iot_button_type, pushed_prop_type, push_counter_prop_type;
static bool pushed = false;

/* ************************************************************
 *
 * button interrupt
 *
 * ***********************************************************/
static void IRAM_ATTR button_isr_handler(void* arg){
	static portBASE_TYPE xHigherPriorityTaskWoken;

	if (button_ready == true){
		xSemaphoreGiveFromISR(button_sem, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR();
		button_ready = false;
		gpio_isr_handler_remove(GPIO_BUTTON);
	}
	irq_counter_1++; //for tests
}


/*************************************************************
 *
 * main button function
 *
 * ************************************************************/
void button_fun(void *pvParameter){

	printf("Button task is ready\n");
	
	button_ready = true;

	for(;;){
		//wait for button pressed
		xSemaphoreTake(button_sem, portMAX_DELAY);
		
		//test
		printf("irq: %i, irq_fun: %i\n", irq_counter_1, irq_counter);
		//test end
		
		//wait a bit to avoid button vibration
		//vTaskDelay(200 / portTICK_PERIOD_MS);
		int button_value = gpio_get_level(GPIO_BUTTON);

		if (button_value == 0){
			//button pressed
			pushed = true;
			irq_counter++;
			if (irq_counter%10 == 0){
				int *c = malloc(sizeof(int));
				*c = 10;
				emit_event(iot_button -> thing_nr, "10times", (void *)c);
			}
		}
		else{
			//button released
			pushed = false;
		}

		inform_all_subscribers_prop(prop_pushed);
		inform_all_subscribers_prop(prop_push_counter);

		//wait a bit to avoid button vibration
		vTaskDelay(200 / portTICK_PERIOD_MS);
		
		//check if button is released meanwhile
		int button_value_1 = gpio_get_level(GPIO_BUTTON);
		if (button_value_1 != button_value){
			if (button_value_1 == 0){
				//button pressed
				pushed = true;
			}
			else{
				//button released
				pushed = false;
			}
			inform_all_subscribers_prop(prop_pushed);
		}
		
		if (button_ready == false){
			button_ready = true;
			gpio_isr_handler_add(GPIO_BUTTON, button_isr_handler, NULL);
		}
	}
}


/*******************************************************************
 *
 * initialize button's GPIO
 *
 * ******************************************************************/
void init_button_io(void){
	gpio_config_t io_conf;

	//interrupt on both edges
	io_conf.intr_type = GPIO_INTR_ANYEDGE;
	//bit mask of the pins, use GPIO4/5 here
	io_conf.pin_bit_mask = GPIO_BUTTON_MASK;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 1;
	io_conf.pull_down_en = 0;
	gpio_config(&io_conf);

	gpio_isr_handler_add(GPIO_BUTTON, button_isr_handler, NULL);
}


/*****************************************************************
 *
 * Initialize button thing and all it's properties and event
 *
 * ****************************************************************/
thing_t *init_button(void){
	//thing_t *iot_button = NULL;

	vSemaphoreCreateBinary(button_sem);
	xSemaphoreTake(button_sem, 0);
	init_button_io();
	
	button_mux = xSemaphoreCreateMutex();
	//create button thing
	iot_button = thing_init();
	
	iot_button -> id = "Button";
	iot_button -> at_context = things_context;
	iot_button -> model_len = 1500;
	//set @type
	iot_button_type.at_type = "PushButton";
	iot_button_type.next = NULL;
	set_thing_type(iot_button, &iot_button_type);
	iot_button -> description = "Internet connected button";
	
	//create pushed property
	prop_pushed = property_init(NULL, NULL);
	prop_pushed -> id = "pushed";
	prop_pushed -> description = "button state";
	pushed_prop_type.at_type = "PushedProperty";
	pushed_prop_type.next = NULL;
	prop_pushed -> at_type = &pushed_prop_type;
	prop_pushed -> type = VAL_BOOLEAN;
	prop_pushed -> value = &pushed;
	prop_pushed -> title = "Pushed";
	prop_pushed -> read_only = true;
	prop_pushed -> set = NULL;
	prop_pushed -> mux = button_mux;

	add_property(iot_button, prop_pushed); //add property to thing
	
	//create push counter property
	prop_push_counter = property_init(NULL, NULL);
	prop_push_counter -> id = "counter";
	prop_push_counter -> description = "button push counter";
	push_counter_prop_type.at_type = "LevelProperty";
	push_counter_prop_type.next = NULL;
	prop_push_counter -> at_type = &push_counter_prop_type;
	prop_push_counter -> type = VAL_INTEGER;
	prop_push_counter -> value = &irq_counter;
	prop_push_counter -> max_value.int_val = INT_MAX;
	prop_push_counter -> min_value.int_val = 0;
	prop_push_counter -> unit = "pcs";
	prop_push_counter -> title = "Counter";
	prop_push_counter -> read_only = true;
	prop_push_counter -> set = NULL;
	prop_push_counter -> mux = button_mux;

	add_property(iot_button, prop_push_counter); //add property to thing
	
	//event "button pushed 10 times"
	ten_times_event = event_init();
	ten_times_event -> id = "10times";
	ten_times_event -> title = "10 times";
	ten_times_event -> description = "button pushed 10 times";
	ten_times_event -> type = VAL_INTEGER;
	ten_times_event -> at_type = "AlarmEvent";
	ten_times_event -> unit = "pcs";

	add_event(iot_button, ten_times_event); //add event to thing
	
	if (button_sem != NULL){
		xTaskCreate(&button_fun, "button_task",
					configMINIMAL_STACK_SIZE * 4, NULL, 0, NULL);
	}

	return iot_button;
}
