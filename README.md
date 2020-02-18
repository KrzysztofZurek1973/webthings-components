# IoT components

This repository includes **Web Thing Server** for ESP32 and things dedicated for this server.

**Web Thing Server** and other IoT things must be placed in `components` directory in esp-idf project directory.

This software uses esp-idf environment with freeRTOS.

## Components:

* **Web Thing Server** - core element, it serves adding things, creating things with it's properties, actions and events. Server supports communication in accordance with [Web Thing API](https://iot.mozilla.org/wot/), both „REST API” and „WebSocket API”. It also supports [WoT Capability Schemas](https://iot.mozilla.org/schemas/).

* **Push Button** - `PushButton` thing with the following parameters:
	- `pushed`: shows if button is pushed or not
	- `counter`: shows how many times button was pushed
	- `10times` (event): it sends event notification when button was pushed 10 times
 
* **Blinking Led** - `Light` thing with the following parameters:
	- `led_on`: ON/OFF switch
	- `frequency`: led blinking frequency
	- `constant_on` (action): turns the led on for defined period of time
	
* **ws2812 controller** - with the following parameters:
	- `on` - ON/OFF led line
	- `diodes` - number of diodes in the line
	- `pattern` - currently running pattern
	- `color` - RGB color defined for some patterns
	- `speed` - pattern refreshment speed, 0 .. 100
	- `brgh` - leds' brightness, 0 .. 100
	
* **thermometer** - based on DS18B20 temperature sensor, properties:
	- `temperature`
	- `errors`
	- `correctness`

For more information, see the individual component folders.

## License

The code in this project is licensed under the MIT license - see LICENSE for details.

## Authors

* **Krzysztof Zurek** - [kz](https://github.com/KrzysztofZurek1973)

