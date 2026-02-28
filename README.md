# ESP32 Weather Station with TFT Display, RSS News and WiFi Clock 🌦️

## Overview

This project is a DIY Weather Station built using an ESP32 microcontroller, a DHT22 temperature and humidity sensor, and a 2.8" TFT color display.

The system shows local environmental data, live news from RSS feeds, and real-time clock and calendar information synchronized over WiFi.

Navigation is done using an analog joystick and a custom menu interface.

This project is currently under construction. The core system is functional and tested on a breadboard, and a permanent enclosure will be designed and built.

---

## Features

* Local temperature measurement (DHT22)
* Local humidity measurement (DHT22)
* Real-time clock and calendar via WiFi (NTP)
* RSS news feeds:

  * BBC News
  * UK News
  * Cork Airport Weather
* Interactive menu system
* Joystick navigation
* TFT graphical display interface

---

## Hardware Used

* ESP32 development board
* DHT22 temperature and humidity sensor
* DollaTek 2.8 inch TFT color display (240x320)
* Analog joystick module
* Breadboard
* Jumper wires

---

## Current Status

Project stage:

* [x] ESP32 configured
* [x] TFT display working
* [x] DHT22 sensor working
* [x] WiFi connection working
* [x] RSS feeds working
* [x] Clock and calendar synchronized via internet
* [x] Menu navigation implemented
* [ ] Ui improve
* [ ] Final enclosure construction
* [ ] Hardware soldering
* [ ] Final assembly

---

## Project Photo (Prototype)

Current breadboard prototype:

![ESP32 Weather Station Prototype](images/esp32-weatherStation.jpeg)


# How It Works

The ESP32 connects to WiFi and retrieves:

    * Current time from NTP servers

    * RSS feeds from online news sources

It also reads local environmental data from the DHT22 sensor.

All information is displayed on the TFT screen through a menu system controlled by a joystick.



# Software

Developed using:

Arduino IDE:

* C++

* ESP32 libraries

* WiFi and HTTPClient libraries

* TFT display libraries

* DHT sensor library


# Future Improvements

* Design and build custom enclosure

* Improve graphical interface

* Add additional sensors (pressure, air quality)

* Add automatic screen rotation

* Optimize power consumption


# Author

### Killo007 ### 

Software Development Student
Embedded Systems and IoT Enthusiast