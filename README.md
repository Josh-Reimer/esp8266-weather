# esp8266-weather
 ESP8266 temperature and humidity logger with telegram bot and web interface
It uses LittleFS for a file system that can store about 3 months worth of temperature, humidity, and a timestamp in a csv file. The csv file can be retrieved through the telegram bot.
I had a hard time getting LittleFS to work. A lot of the file system code in this repository are from random esp32 and esp8266 forums that I can't recall anymore.
Also don't try this with platformIO. It works best with ArduinoIDE.
