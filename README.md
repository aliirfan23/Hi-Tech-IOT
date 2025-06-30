# IoT Projects Suite

A concise collection of Arduino-based IoT sketches for sensor monitoring and measurement systems.

## Contents

* **Rawat\_broooder.ino**: Poultry brooder temperature and humidity monitor (DHT sensor).
* **manderah\_sensor.ino**: Environmental sensor data logger for the Mandra site.
* **port\_testor.ino**: Serial port tester and debug utility.
* **rawat\_fuel\_weight.ino**: Fuel weight measurement with load cell & HX711.
* **weight\_machine\_mandra.ino**: Grain weight machine interface (load cell & HX711).
* **load\_cell\_final.ino**: Finalized load cell calibration and data acquisition.
* **combined\_gen\_load.ino**: Combined generator runtime and load weight logger.
* **diesal\_generatorV2.ino**: Diesel generator runtime and fuel usage logger (RTC + SD card).

## Hardware Requirements

* Arduino Uno (or compatible)
* DHT11/22 sensor
* Load cell + HX711 amplifier
* RTC module (DS1307/DS3231)
* SD card module
* Jumper wires, breadboard, power supply

## Setup

1. Install Arduino IDE (1.8+).
2. In **Tools > Manage Libraries**, install:

   * `DHT sensor library`
   * `HX711`
   * `RTClib`
3. Wire sensors and modules as per comments in each sketch.

## Usage

1. Open the desired `.ino` file in the Arduino IDE.
2. Select the correct board and COM port under **Tools**.
3. Upload the sketch.
4. Open the Serial Monitor (baud rate set in the code) to view live data.
5. For SD logging sketches, retrieve the generated `.csv` files from the SD card.

## File Descriptions

| File                        | Description                                                |
| --------------------------- | ---------------------------------------------------------- |
| Rawat\_broooder.ino         | Monitors temperature & humidity in a poultry brooder.      |
| manderah\_sensor.ino        | Logs environmental data at the Mandra site.                |
| port\_testor.ino            | Tests and displays serial port data for debugging.         |
| rawat\_fuel\_weight.ino     | Measures fuel weight and outputs live readings.            |
| weight\_machine\_mandra.ino | Reads grain weight from a load cell attached to a machine. |
| load\_cell\_final.ino       | Calibrated load cell data acquisition with filtering.      |
| combined\_gen\_load.ino     | Records generator runtime and load cell weight together.   |
| diesal\_generatorV2.ino     | Logs generator start/stop times and calculates fuel usage. |

## License
Open to use

---

*Created by Ali Irfan Mirza*
