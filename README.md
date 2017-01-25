# piook
Raspberry Pi On-Off Keying Decoder for the CliMET 433MHz weather station

This is a C++ program that can be used to receive and decode radio transmissions from a cliMET weather station.

#### CliMET Weather Station

The [Climet CM9088 Weather Station](https://www.climemet.com/products/cm9088-temperature-and-humidity-forecast-station) consists of a small
'remote' module (model number CM7-TX) that should be mounted outside to record temperature and relative humidity. This module periodically
(every minute or so) transmits a short burst of data to the main module which has an LCD that displays various info, including the data from 
the remote module.

Radio transmission from the remote module occurs on 433Mhz using [on-off keying](https://en.wikipedia.org/wiki/On-off_keying), a crude but simple
data transmission scheme in which the transmitter is simply switched on and off very quickly with well defined off intervals defining the binary
bits of the data sequence.

#### 433MHz Radio Receiver

433MHz is a standardised band for low power transmission; devices using [on-off keying](https://en.wikipedia.org/wiki/On-off_keying) 
on this band are common, e.g. wireless doorbells, thermostats and electricty meters typically will use this band, as such 433MHz
receiver and transmitter modules are widely available.

I initially attempted this project using the widely available and very cheap [XY-MK-5V](http://www.guillier.org/blog/2014/10/new-rf-433mhz-receiver/) 
receivers, which appear to be a simple regenerative or superregenerative type receiver; a primitive circuit with low sensitivity and selectivity, 
and indeed I was unable to get these to receive a clean signal beyond about 5 meters.

Web research lead to the [RXB6](http://www.guillier.org/blog/2014/10/new-rf-433mhz-receiver/) a superior superheterodyne type receiver.

#### Connecting to a Raspberry Pi

Both the XY-MK-5V and RXB6 have simple physical interfaces consisting of a +3 or +5V power supply, ground, and a data/signal line,
which typically is wired to the positive power rail via a [pull up resistor](https://en.wikipedia.org/wiki/Pull-up_resistor) to prevent 
a floating voltage on the data line; I used a 10k Ohm resistor.

The data line can then be connected to one of the Pi's GPIO pins, and we can interface to these pins in software using the very handy 
[wiringPi](http://wiringpi.com/) project.


#### Compiling piook

piook consists is just piook.cpp and the associated header file, piook.h. The only dependency is wiringPi, for which installation instructions
can be found at http://wiringpi.com/download-and-install/. Brifly though the steps are:

First check if wiringPi is already installed with:

    gpio -v

If not then:

    git clone git://git.drogon.net/wiringPi
    cd ~/wiringPi
    git pull origin
    ./build

The compiled library will be installed in the relevant system library folder, so from now on we can link to wiring pi with `gcc -lwiringPi` or whatever.

To compile piook, in the folder containing piook.cpp:

    g++ piook.cpp -lwiringPi -O3 -o piook

The -O3 option is optional, this is the highest optimisation setting.
-o piook is just the binary executable output.


#### Running piook (Usage)

Usage:
    piook pinNumber outfile

pinNumber: GPIO pin number (wiringPi number scheme) to listen on. Based on the wiringPi 'simplified' pin numbering scheme (see defined at http://wiringpi.com/pins/)
outfile: filename to write data to.

 * Must be called with root privileges.
 * piook will listen on the specified pin for valid OOK sequences from the cliMET weather station.
 * Valid sequences are decoded to a temperature in Centigrade, and a relative humidity (RH%) value.
 * The decoded data is written to the output file in the format: temp,RH with a newline (\n) terminator
 * Each update overwrites the previous file, i.e. the file will always contain a single line
    containing the most recently received data.
 * Project URL: http://github.com/colgreen/piook


#### Theory of Operation 

How this program works is actually rather dumb and inefficient, but it does work, read on...

The data line from the 433 MHz receiver module will receive random noise when no transmission is in progress, hence the
voltage on the GPIO pin will just be noise. When a transmission begines the voltage will follow the pattern of the on-off
pulses.

piook attaches an interrupt handler to the GPIO pin which fires each time the voltage transistions between the low and 
high states, hence when there is noise on the pin the interrupt handler is being constantly called, possible thousands of times 
per second, certainly hundreds. The interrupt handler records the time since it was last called and maintains a circular buffer 
of previous times between interrupts. 

Each transmission has a preamble, which is a fixed data sequence we can use to detect when a transmission has begun. So on each 
interrupt call we push an entry onto the circular buffer and test if the preamble is present in that buffer, if so then we can 
switch into a receiving mode.


#### Reverse Engineering the Data Modulation and Encoding

Message format was determined partly from internet searching and partly from reverse engineering the received signals. A raw signal 
can be recorded and manually examined by attching the data pin of the receiver to an audio line in of a PC and using audio recording 
software (I used [Audacity](http://www.audacityteam.org/) on Windows) to record the raw pulse train. By comparing the pulse trains
with the temperature and humidity readouts on the indoor modules we can gradually determine, firstly where the temp and RH data is located
and with some effort, how those values are encoded. In order to determine how negative temperature were encoded it was necessary to place 
the remote module in a freezer for a brief time. Similarly, the high end of the temperature range was tested by using an oven on a low heat.

Analysis of the raw pulse trains yielded the following knowledge:

   * Each on pulse has duration 1000 µs (1 millisecond).
   * There are two different durations of off pulse, 500 and 1,500 µs
   * The short (500 µs) off pulse represents a binary 1.
   * The long (1,500 µs) off pulse represents a binary 0.

With this knowledge we can now decode the series of interrupts into a binary data sequence and begin to examine its format
and how it varies in conjunction with the know temperature and RH readings from the indoor module.


#### Message format

The format is as follows
     
      



