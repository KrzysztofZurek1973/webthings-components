/********************************************************************
 *
 * IoT device: thermometer on ESP32 with DS18B20 sensor
 * Compatible with Web Thing API
 *
 * thing_termometer.c
 *
 *  Created on:		Oct 16, 2019
 * Last update:		Feb 17, 2020
 *      Author:		kz
 *		E-mail:		krzzurek@gmail.com
 * -------------------------------------------------------
 * 1-wire from:		github.com/DavidAntliff/esp32-ds18b20-example
 *
 ********************************************************************/
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "simple_web_thing_server.h"

//1-wire
#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"

//include dummy or real OTA updater
#include "esp32_ota_updater.h"

//1-wire DS18B20, digital temperature sensor constants
#define GPIO_DS18B20_0       	(CONFIG_ONE_WIRE_GPIO)
#define MAX_DEVICES          	(3)
#define DS18B20_RESOLUTION   	(DS18B20_RESOLUTION_12_BIT)
#define DS18B20_SAMPLE_PERIOD	(1000)   // milliseconds
#define TEMP_SAMPLES			(5)

static OneWireBus * owb;
static DS18B20_Info *devices[MAX_DEVICES] = {0};
static owb_rmt_driver_info rmt_driver_info;

xSemaphoreHandle therm_mux;
xTaskHandle thermometer_task; //task for reading temperature

//THINGS AND PROPERTIES
static double temperature = 0.0, last_sent_temperature = 0.0;
//5 readings without errors mean 100% correctness
static int temp_correctness = 0, old_temp_correctness = 0;
static int temp_errors = 0, old_temp_errors = 0;
thing_t *thermometer = NULL;
property_t *prop_temperature, *prop_errors, *prop_correctness;
at_type_t therm_type;
at_type_t temp_prop_type;
at_type_t corr_prop_type;
at_type_t errors_prop_type;

//thermometer thread functions
void thermometer_fun(void *param); //thread function


/****************************************************************
 *
 * DS18B20 initialization
 *
 * **************************************************************/
int init_ds18b20(void){
	// Create a 1-Wire bus, using the RMT timeslot driver
	int num_devices = 0;

	vTaskDelay(2000 / portTICK_PERIOD_MS);

	owb = owb_rmt_initialize(&rmt_driver_info, GPIO_DS18B20_0, RMT_CHANNEL_1, RMT_CHANNEL_0);
	owb_use_crc(owb, true);  // enable CRC check for ROM code

	// Find all connected devices
	printf("Find devices:\n");
	OneWireBus_ROMCode device_rom_codes[MAX_DEVICES] = {0};
	OneWireBus_SearchState search_state = {0};
	bool found = false;
	owb_search_first(owb, &search_state, &found);
	while (found){
		char rom_code_s[17];
		owb_string_from_rom_code(search_state.rom_code, rom_code_s, sizeof(rom_code_s));
		printf("  %d : %s\n", num_devices, rom_code_s);
		device_rom_codes[num_devices] = search_state.rom_code;
		++num_devices;
		owb_search_next(owb, &search_state, &found);
	}
	printf("Found %d device%s\n", num_devices, num_devices == 1 ? "" : "s");

	if (num_devices == 1){
		// For a single device only:
		OneWireBus_ROMCode rom_code;
		owb_status status = owb_read_rom(owb, &rom_code);
		if (status == OWB_STATUS_OK){
			char rom_code_s[OWB_ROM_CODE_STRING_LENGTH];
			owb_string_from_rom_code(rom_code, rom_code_s, sizeof(rom_code_s));
			printf("Single device %s present\n", rom_code_s);
		}
		else{
			printf("An error occurred reading ROM code: %d", status);
		}
	}
	else{
		// Search for a known ROM code (LSB first):
		// For example: 0x1502162ca5b2ee28
		OneWireBus_ROMCode known_device = {
				.fields.family = { 0x28 },
				.fields.serial_number = { 0xee, 0xb2, 0xa5, 0x2c, 0x16, 0x02 },
				.fields.crc = { 0x15 },
		};
		char rom_code_s[OWB_ROM_CODE_STRING_LENGTH];
		owb_string_from_rom_code(known_device, rom_code_s, sizeof(rom_code_s));
		bool is_present = false;

		owb_status search_status = owb_verify_rom(owb, known_device, &is_present);
		if (search_status == OWB_STATUS_OK){
			printf("Device %s is %s\n", rom_code_s, is_present ? "present" : "not present");
		}
		else{
			printf("An error occurred searching for known device: %d", search_status);
		}
	}

	// Create DS18B20 devices on the 1-Wire bus
	for (int i = 0; i < num_devices; ++i){
		// heap allocation
		DS18B20_Info *ds18b20_info = ds18b20_malloc();
		devices[i] = ds18b20_info;

		if (num_devices == 1){
			printf("Single device optimizations enabled\n");
			// only one device on bus
			ds18b20_init_solo(ds18b20_info, owb);
		}
		else{
			// associate with bus and device
			ds18b20_init(ds18b20_info, owb, device_rom_codes[i]);
		}
		// enable CRC check for temperature readings
		ds18b20_use_crc(ds18b20_info, true);
		ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION);
	}
	return num_devices;
}


/**********************************************************
 *
 * temperature reading task
 *
 * ********************************************************/
void thermometer_fun(void *param){
	int sample_nr = 0;
	float readings[MAX_DEVICES] = { 0 };
	DS18B20_ERROR errors[MAX_DEVICES] = { 0 };
	double temp_sum = 0;
	int correct_samples = 0;
	int num_devices = 0;
	time_t time_now, time_prev;

	printf("thermometer task is starting\n");
	num_devices = init_ds18b20();
	vTaskDelay(500 / portTICK_PERIOD_MS);
	time(&time_now);
	time_prev = time_now;

	if (num_devices > 0){
		printf("DS18B20 sensors are found\n");
		TickType_t last_wake_time = xTaskGetTickCount();

		for (;;){
			last_wake_time = xTaskGetTickCount();

			ds18b20_convert_all(owb);
			ds18b20_wait_for_conversion(devices[0]);

			for (int i = 0; i < num_devices; ++i){
				errors[i] = ds18b20_read_temp(devices[i], &readings[i]);
			}

			//read first temperature (only one sensor is available in this version)
			if (errors[0] == DS18B20_OK){
				correct_samples++;
				temp_sum += (double)readings[0];
			}
			else{
				temp_errors++;
			}

			//TODO: sensor's error signaling
			sample_nr++;
			if (sample_nr%TEMP_SAMPLES == 0){
				//set new temperature
				temperature = temp_sum / correct_samples;
				time(&time_now);
				int dt = abs((int)((temperature - last_sent_temperature)*100)); 
				//printf("dT = %i, dt = %i\n", dt, (int)(time_now - time_prev));
				if ((dt >= 10) || ((time_now - time_prev) >= 30)){
					int8_t s = inform_all_subscribers_prop(prop_temperature);
					if (s == 0){
						last_sent_temperature = temperature;
						time_prev = time_now;
						//printf("temperature sent, T = %f\n", temperature);
					}
					else{
						//printf("temperature NOT sent, T = %f\n", temperature);
					}
				}
				//set new correctness
#ifdef CONFIG_ENABLE_OTA_UPDATE
				if (ota_update_block() == OTA_BLOCK_OK){
#endif
					temp_correctness = (100 * correct_samples)/TEMP_SAMPLES;
					if (temp_correctness != old_temp_correctness){
						inform_all_subscribers_prop(prop_correctness);
						old_temp_correctness = temp_correctness;
					}
					//set new errors
					if (temp_errors != old_temp_errors){
						inform_all_subscribers_prop(prop_errors);
						old_temp_errors = temp_errors;
					}
#ifdef CONFIG_ENABLE_OTA_UPDATE
				}
				ota_update_unblock();
#endif

				//reset other values
				temp_sum = 0;
				correct_samples = 0;
			}

			vTaskDelayUntil(&last_wake_time, DS18B20_SAMPLE_PERIOD / portTICK_PERIOD_MS);
		}
	}
	else{
		printf("No devices found\n");
	}

	// clean up dynamically allocated data
	for (int i = 0; i < num_devices; ++i){
		ds18b20_free(&devices[i]);
	}
	owb_uninitialize(owb);
}


/*******************************************************
 *
 * IoT thing initialization
 *
 * ******************************************************/
thing_t *init_thermometer(char *_thing_id){

	//start thing
	therm_mux = xSemaphoreCreateMutex();
	//create thing 1, counter of seconds ---------------------------------
	thermometer = thing_init();

	thermometer -> id = _thing_id;
	thermometer -> at_context = things_context;
	thermometer -> model_len = 1500;
	//set @type
	therm_type.at_type = "TemperatureSensor";
	therm_type.next = NULL;
	set_thing_type(thermometer, &therm_type);
	thermometer -> description = "Indoor thermometer";

	//create temperature property
	prop_temperature = property_init(NULL, NULL);
	prop_temperature -> id = "temperature";
	prop_temperature -> description = "temperature sensor DS18B20";
	temp_prop_type.at_type = "TemperatureProperty";
	temp_prop_type.next = NULL;
	prop_temperature -> at_type = &temp_prop_type;
	prop_temperature -> type = VAL_NUMBER;
	prop_temperature -> value = &temperature;
	prop_temperature -> max_value.float_val = 125.0;
	prop_temperature -> min_value.float_val = -55.0;
	prop_temperature -> unit = "degree celsius";
	prop_temperature -> title = "Temperature";
	prop_temperature -> read_only = true;
	prop_temperature -> set = NULL;
	prop_temperature -> mux = therm_mux;

	add_property(thermometer, prop_temperature); //add property to thing

	//create correctness property
	prop_correctness = property_init(NULL, NULL);
	prop_correctness -> id = "correctness";
	prop_correctness -> description = "temperature correctness";
	corr_prop_type.at_type = "LevelProperty";
	corr_prop_type.next = NULL;
	prop_correctness -> at_type = &corr_prop_type;
	prop_correctness -> type = VAL_INTEGER;
	prop_correctness -> value = &temp_correctness;
	prop_correctness -> max_value.int_val = 100;
	prop_correctness -> min_value.int_val = 0;
	prop_correctness -> unit = "percent";
	prop_correctness -> title = "Correctness";
	prop_correctness -> read_only = true;
	prop_correctness -> set = NULL;
	prop_correctness -> mux = therm_mux;

	add_property(thermometer, prop_correctness); //add property to thing

	//create errors property
	prop_errors = property_init(NULL, NULL);
	prop_errors -> id = "errors";
	prop_errors -> description = "temperature reading errors";
	errors_prop_type.at_type = "LevelProperty";
	errors_prop_type.next = NULL;
	prop_errors -> at_type = &errors_prop_type;
	prop_errors -> type = VAL_INTEGER;
	prop_errors -> value = &temp_errors;
	prop_errors -> unit = "";
	prop_errors -> title = "Errors";
	prop_errors -> read_only = true;
	prop_errors -> set = NULL;
	prop_errors -> mux = therm_mux;

	add_property(thermometer, prop_errors); //add property to thing

	xTaskCreate(&thermometer_fun, "thermometer", configMINIMAL_STACK_SIZE * 4, NULL, 5, &thermometer_task);

	//start thread
	return thermometer;
}
