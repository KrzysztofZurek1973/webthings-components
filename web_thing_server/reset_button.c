#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "reset_button.h"

//NVS wifi reset button
#define GPIO_RESET_BUTTON			CONFIG_RESET_GPIO
#define GPIO_RESET_BUTTON_MASK		(1ULL << GPIO_RESET_BUTTON)

static xSemaphoreHandle DRAM_ATTR reset_button_sem;
//static bool DRAM_ATTR nvs_reset_button_ready = false;


/* ************************************************************
 *
 * button interrupt
 *
 * ***********************************************************/
static void IRAM_ATTR reset_button_isr_handler(void* arg){
	static portBASE_TYPE xHigherPriorityTaskWoken;

	xSemaphoreGiveFromISR(reset_button_sem, &xHigherPriorityTaskWoken);
	if (xHigherPriorityTaskWoken == true){
		portYIELD_FROM_ISR();
	}
}


/*************************************************************
 *
 *
 *
 * ************************************************************/
void reset_button_fun(void *pvParameter){
	esp_err_t err;
	nvs_handle storage_handle = 0;

	printf("Button task is ready\n");
	gpio_intr_enable(GPIO_RESET_BUTTON);

	for(;;){
		//wait for button pressed
		xSemaphoreTake(reset_button_sem, portMAX_DELAY);
		//wait a bit to avoid button vibration
		vTaskDelay(400 / portTICK_PERIOD_MS);
		int gpio_value = gpio_get_level(GPIO_RESET_BUTTON);

		if (gpio_value == 0){
			ESP_LOGW("RESET", "Delete wifi data in NVS");
			err = nvs_open("storage", NVS_READWRITE, &storage_handle);
			if (err == ESP_OK){

				nvs_erase_key(storage_handle, "ssid");
				nvs_erase_key(storage_handle, "pass");
				nvs_erase_key(storage_handle, "mdns_host");

				printf("Committing updates in NVS ... ");
				err = nvs_commit(storage_handle);
				printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

				nvs_close(storage_handle);
				
				//restart node
				fflush(stdout);
				esp_wifi_stop();
				esp_restart();
				//node_restart = true;
			}
			else{
				printf("NVS failed to open: %s\n", esp_err_to_name(err));
			}
		}
	}
}


/*******************************************************************
 *
 * initialize reset button
 * this button deletes wifi parameters stored in NVS memory
 *
 * ******************************************************************/
void init_reset_button_io(void){
	gpio_config_t io_conf;

	//interrupt on falling edge
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
	//bit mask for the pin
	io_conf.pin_bit_mask = GPIO_RESET_BUTTON_MASK;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf.pull_up_en = 1;
	io_conf.pull_down_en = 0;
	gpio_config(&io_conf);

	//install gpio isr service
	gpio_intr_disable(GPIO_RESET_BUTTON);
	gpio_isr_handler_add(GPIO_RESET_BUTTON, reset_button_isr_handler, NULL);
}


/*********************************************************
 *
 *
 *
 * *******************************************************/
void init_reset_button(void){

	reset_button_sem = xSemaphoreCreateBinary();
	init_reset_button_io();

	if (reset_button_sem != NULL){
		xTaskCreate(&reset_button_fun, "reset_task", configMINIMAL_STACK_SIZE * 2, NULL, 0, NULL);
	}

}
