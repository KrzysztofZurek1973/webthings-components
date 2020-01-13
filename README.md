# IoT components

This repository includes „Web Thing Server” for ESP32 and things dedicated for this server.

## Components:

 * „Web Thing Server” - core element, it serves adding things, creating things with it's properties, actions and events. Server supports communication in accordance with [Web Thing API](https://iot.mozilla.org/wot/), both „REST API” and „WebSocket API”. It also supports [WoT Capability Schemas](https://iot.mozilla.org/schemas/).

 * „Push Button” - „PushButton” thing with following parameters:
	- property „pushed” (shows if button is pushed or not),
	- property „counter” (shows how many times button was pushed).	
 
 * „Blinking Led” - „Light” thing with following parameters:
	- property „led_on” (ON/OFF switch),
	- property „frequency” (led blinking frequency),
	- action „constant_on” (turns the led on for defined period of time).

For more information, see the individual component folders.

## License

The code in this project is licensed under the MIT license - see LICENSE for details.

## Authors

* **Krzysztof Zurek** - [kz](https://github.com/KrzysztofZurek1973)

