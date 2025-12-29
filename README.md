# piook
Raspberry Pi On-Off Keying (OOK) Decoder for the ClimeMET Weather Station, model CM9088 (with 433MHz remote/outdoor module).

## Overview

piook is a small program for the [Raspberry Pi](https://en.wikipedia.org/wiki/Raspberry_Pi) computer that will decode radio transmissions from a ClimeMET weather station remote module, in conjunction with a cheap 433 MHz radio receiver module wired to one of the Raspberry Pi's GPIO pins.


### ClimeMET Weather Station

The [ClimeMET CM9088 Weather Station](https://www.climemet.com/products/cm9088-temperature-and-humidity-forecast-station) consists of two items; an indoor module with an LCD display, and a remote/outdoor module (part number [CM7-TX](https://www.climemet.com/products/cm7-tx-temperature-transmitter)) for mounting on an outside wall for taking readings of the external temperature and [relative humidity](https://en.wikipedia.org/wiki/Relative_humidity) (RH).

The remote module periodically (approximately per minute) transmits a short burst of data to the main module for indoor display of the external readings. Radio transmission occurs at 433 MHz using [on-off keying](https://en.wikipedia.org/wiki/On-off_keying), a crude but simple data transmission scheme in which the transmitter is simply switched on and off very quickly with well defined off intervals defining the binary bits of the data sequence being transmitted.


### 433 MHz Radio Receivers

433MHz is a standard band for low power transmission; devices using [on-off keying](https://en.wikipedia.org/wiki/On-off_keying) on this band are common e.g. wireless doorbells, thermostats and electricity meters typically use this band. As such cheap 433 MHz receiver and transmitter modules are widely available.

I initially attempted this project using the widely available and very cheap [XY-MK-5V](http://www.guillier.org/blog/2014/10/new-rf-433mhz-receiver/) receivers, which appear to be a simple regenerative or superregenerative type receiver - a primitive circuit with low sensitivity and selectivity, and indeed I was unable to get these to receive a clean signal beyond about 5 meters from the transmitter.

Web research lead to the [RXB6](http://www.guillier.org/blog/2014/10/new-rf-433mhz-receiver/), a superior superheterodyne type 433 MHz receiver module; this module allows reception of the OOK signal inside the house from the outdoor module located about 10 meters away.


### Connecting to a Raspberry Pi

Both the XY-MK-5V and RXB6 have simple physical interfaces consisting of +5V and ground connectors for power, and a single data/signal connector that should typically by wired to the positive power rail via a [pull up resistor](https://en.wikipedia.org/wiki/Pull-up_resistor) to prevent a floating voltage on the data line; I used a 10k Ohm resistor for this.

The data line can then be connected to one of the Raspberry Pi's GPIO pins which we can interface to in software using [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/).


### Dependencies

This version requires libgpiod. On Raspberry Pi OS/Debian/Ubuntu:

```bash
sudo apt update
sudo apt install libgpiod-dev
```

On other distributions, install libgpiod from your package manager or build from source.


### Building piook

piook consists of three files: `piook.c`, `decoder.c`, and `piook.h`. The only dependency is libgpiod.

#### Using Make (recommended)

```bash
make
```

This will build the `piook` executable.

#### Manual compilation

```bash
gcc -Wall -Wextra -O3 -std=gnu99 -march=native src/piook.c src/decoder.c -lgpiod -o piook
```

#### Installation

To install system-wide (requires root):

```bash
sudo make install
```

To uninstall:

```bash
sudo make uninstall
```


### Running piook (Usage)

Usage:

```
piook [OPTIONS]

Options:
  -h, --help                 Show help message
  -v, --verbose              Enable verbose output
  -p, --pin PIN              GPIO line number (default: 7)
  -o, --output FILE          Output filename (required)
  -c, --chip CHIP            GPIO chip name (default: gpiochip0)

Examples:
  piook -p 17 -o weather.txt
  piook --verbose --chip gpiochip1 -p 23 -o data.txt
  piook -o weather.txt  (use default pin 7)
```

Notes:
 * Must be called with privileges to access /dev/gpiochip* (usually root or gpio group).
 * piook will listen on the specified gpio line for valid OOK sequences being received by the attached radio module.
 * Valid sequences are decoded to a temperature (in Centigrade), and a relative humidity (RH%) value.
 * The temperature encoding has range -204.7 to +204.7, and precision of 0.1.
 * Relative humidity has range 0-100, and precision of 1.0.
 * The decoded data is written to the output file in the format: `temp,RH` with a newline (\n) terminator.
 * Each received transmission overwrites the previous file, i.e. the file will always contain a single line
   containing the most recently received data.


### Data Modulation

Each transmission consists of a series of on-off transitions of the transmitter, these are observed on the data
pin of the radio receiver module as a series of voltage transition steps. The steps encode binary bits; analysis
of multiple transmissions yielded the following knowledge:

   * Each on-pulse has duration 1000 µs (1 millisecond).
   * There are two different durations of off-pulse, 500 and 1,500 µs
   * The short (500 µs) off-pulse represents a binary 1.
   * The long (1,500 µs) off-pulse represents a binary 0.


### Message Format

Each transmission conveys 48 bits of data (6 bytes or 12 nibbles). 

The message format is as follows:

byte# |nibble# | designation
----- | ------ | ----------- 
0     | 0 | Fixed preamble 0xF
0     | 1 | Fixed preamble 0xF
1     | 2 | Fixed preamble 0x8
1     | 3 | Random code/ID (1st nibble)
2     | 4 | Random code/ID (2nd nibble)
2     | 5 | Temperature (1st nibble)
3     | 6 | Temperature (2nd nibble)
3     | 7 | Temperature (3rd nibble)
4     | 8 | Humidity (1st nibble)
4     | 9 | Humidity (2nd nibble)
5     | 10| Checksum (1st nibble)
5     | 11| Checksum (2nd nibble)

Notes.

   * The first three nibbles can be considered to be a fixed sequence/preamble that can be used to detect the start of a message; however, the first nibble tends to be masked with noise, hence piook tests for the sequence 0xF8 instead of 0xFF8. I imagine additional leading bits are  present to allow for just such lead-in-noise issues.

   * Nibbles 3 and 4 straddle a byte boundary and contain a unique code that is generated by the remote unit when it is powered on. I haven't examined how this code changes to observe if there is any pattern, but I it's possible that it's randomly generated. The purpose of this code is to 'lock' the remote unit to the indoor receiver unit. The indoor unit will learn the first code it receives and will ignore any messages with a different code (e.g. from a neighbour who happens to have the same type of module). piook does not use this code.

   * Nibbles 5,6,7 convey a temperature reading. We can decode to an 11 bit integer by appending the least significant 3 bits of nibble 5 with all the bits of nibbles 6 and 7. This gives an integer value of between 0 and (2^11)-1 = 2027. Dividing this by ten gives the magnitude of the temperature. The next least significant bit of nibble 5 indicates the sign, 0 => +ve, 1 => -ve. Hence in principle this encoding scheme can encode temperatures from -204.7 to +204.7 degrees centigrade, with precision of 0.1 degrees C.
   
   * Nibbles 8 and 9 convey humidity encoded as as an unsigned integer. Humidity has range 0 to 100, precision 1, hence values above 100 aren't used.

   * Nibbles 10 and 11 are a checksum. The checksum is a CRC8 variant. See the source code for precise details. 

   

### Theory of Operation 

How this program works is actually rather dumb and inefficient, but it does work, read on...

The data line from the 433 MHz receiver module will receive random noise when no transmission is in progress, hence the voltage on the GPIO pin will usually be noise. When a transmission begins the voltage will follow the pattern of the on-off pulses.

piook attaches an event handler to the GPIO pin, which executes upon each low-high or high-low voltage transition, hence when there is noise on the pin the event handler is being constantly called, possibly thousands of times per second, certainly hundreds. The event handler records the time since it was last called and this allows it to determine pulse durations and therefore if the transition corresponds to a binary 0, 1, or noise. For 0's and 1's the bits are stored in a buffer until there are enough to pass to the decode subroutine.


### Reverse Engineering the Data Modulation and Encoding

The message format was determined partly from internet searching and partly from reverse engineering the received signals. A raw signal can be recorded and manually examined by attaching the data pin of the receiver to an audio line-in of a PC and using audio recording software (I used [Audacity](http://www.audacityteam.org/) on Windows) to record the raw pulse trains.

By comparing the pulse trains with the temperature and humidity readouts on the indoor modules we can gradually determine where the temp and RH data is located in the data sequence, and eventually, how those values are modulated and encoded. In order to determine how negative temperatures are encoded it was necessary to place the remote module in a freezer for a brief time. Similarly, the high end of the temperature range was tested by using an oven on a low heat.

Notable resources:

   * http://lucsmall.com/2012/04/27/weather-station-hacking-part-1/
   * http://lucsmall.com/2012/04/29/weather-station-hacking-part-2/
   * http://www.princetronics.com/how-to-read-433-mhz-codes-w-raspberry-pi-433-mhz-receiver/
   * http://www.susa.net/wordpress/2012/08/raspberry-pi-reading-wh1081-weather-sensors-using-an-rfm01-and-rfm12b/


Colin,<br/>
January 26th, 2017<br/>
<br/>
Revised and updated December 28th, 2025
