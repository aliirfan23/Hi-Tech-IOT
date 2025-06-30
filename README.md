# IoT Projects Suite

A concise collection of four Arduino-based IoT applications for monitoring and measurement tasks.

## Contents

* **Rawat\_broooder.ino**: Poultry brooder temperature and humidity monitor using DHT sensor.
* **rawat\_fuel\_weight.ino**: Fuel weight measurement system with load cell and HX711.
* **weight\_machine\_mandra.ino**: Grain weight machine interface using load cell and HX711.
* **diesal\_generatorV2.ino**: Diesel generator run-time and fuel usage logger with RTC and SD card.

## Hardware Requirements

* Arduino Uno (or compatible)
* DHT11/22 temperature & humidity sensor (for brooder)
* Load cell + HX711 amplifier (for weight measurements)
* RTC module (DS1307 or DS3231) for timestamping (generator logger)
* SD card module (for data logging)
* Jumper wires, breadboard, power supply

## Setup

1. Install Arduino IDE (version 1.8+).
2. In **Sketch > Include Library > Manage Libraries**, install:

   * `DHT sensor library`
   * `HX711`
   * `RTClib`
3. Connect sensors and modules as per each sketch’s comments.

## Usage

1. Open the desired `.ino` file in the Arduino IDE.
2. Select the correct board and COM port under **Tools**.
3. Upload the sketch.
4. Open the Serial Monitor (baud rate set in the code) to view real-time data.
5. For SD logging sketches, check the SD card for saved `.csv` files.

## File Descriptions

| File                        | Functionality                                                    |
| --------------------------- | ---------------------------------------------------------------- |
| Rawat\_broooder.ino         | Reads DHT sensor, displays temperature & humidity                |
| rawat\_fuel\_weight.ino     | Measures fuel weight, outputs live weight on Serial Monitor      |
| weight\_machine\_mandra.ino | Interfaces with grain weight machine, displays weight readings   |
| diesal\_generatorV2.ino     | Logs generator ON/OFF times, start/end weights, calculates usage |

## License

This collection is released under the MIT License. Feel free to adapt for your own projects.
