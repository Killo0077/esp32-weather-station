# ESP32 Weather Station with TFT Display, RSS News and WiFi Clock 🌦️

## Overview

This project is a DIY Weather Station built using an ESP32 microcontroller, a DHT22 temperature and humidity sensor, and a 2.8" TFT color display.

The system displays:

• Local environmental data  
• Live news from RSS feeds  
• Real-time clock and calendar synchronized over WiFi  

Navigation is done using hardware buttons and a custom menu interface.

The project is currently under development. The core system is functional and tested on a breadboard. A permanent enclosure will be designed and built.

---

## Features

- Local temperature measurement (DHT22)
- Local humidity measurement (DHT22)
- Real-time clock via WiFi (NTP)
- RSS news feeds:
  - BBC News
  - UK News
- Cork Airport live weather data
- Interactive menu system
- Custom graphical TFT interface

---

## Hardware Used

- ESP32 Dev Module
- DHT22 Temperature & Humidity Sensor
- 2.8" ILI9341 TFT Display (240x320)
- Push buttons (menu navigation)
- Breadboard
- Jumper wires

---

## Current Status

Project stage:

- [x] ESP32 configured
- [x] TFT display working
- [x] WiFi connection working
- [x] RSS feeds working
- [x] Clock synchronized via NTP
- [x] Menu navigation implemented
- [ ] UI improvements
- [ ] Final enclosure
- [ ] Hardware soldering
- [ ] Final assembly

---
## Project Photos

### Breadboard Prototype
![Breadboard Prototype](images/esp32-weatherStation.jpeg)

### Project Build
![ESP32 Weather Station Prototype](images/IMG_1270.jpeg)
![ESP32 Weather Station Prototype](images/IMG_1272.jpeg)
![ESP32 Weather Station Prototype](images/IMG_1273.jpeg)

### Airport Weather Screen
![Airport weather](images/IMG_1274.jpeg)

### BBC News Screen
![BBC news](images/IMG_1276.jpeg)

### RSS News Screen
![RSS News Screen](images/IMG_1277.jpeg)
---

## How It Works

The ESP32 connects to WiFi and retrieves:

• Current time from NTP servers  
• RSS feeds from online news sources  

It also reads environmental data from the DHT22 sensor.

All information is displayed on the TFT screen through a menu-driven interface.

---

## Software

Developed using:

- Arduino IDE
- C++
- ESP32 WiFi libraries
- HTTPClient
- Adafruit ILI9341 TFT library
- DHT sensor library

---

## Future Improvements

- Custom 3D printed enclosure
- Improved graphical interface
- Add air quality sensor (BME680)
- Add LED status indicators
- Add buzzer for weather alerts
- Optimize memory usage

---

## Author

**Killo0077**

Software Development Student  
Embedded Systems & IoT Enthusiast