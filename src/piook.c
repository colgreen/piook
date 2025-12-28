#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <gpiod.h>
#include <unistd.h>
#include "piook.h"

#define MAX_BIT_COUNT 128
#define EXPECTED_DATA_LEN 5
#define PREAMBLE_LEN 8
#define PREAMBLE_SEQ {1, 1, 1, 1, 2, 1, 2, 2}

// GPIO Pin to monitor.
int g_pinNumber = 7;
char* g_outputFilename;

int main(int argc, char *argv[])
{
    // Parse command line options.
    parseOptions(argc, argv);

    const char *chipname = "gpiochip0"; // default gpiochip
    struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        exit(1);
    }
    printf("Opened GPIO chip %s\n", chipname);

    struct gpiod_line *line = gpiod_chip_get_line(chip, g_pinNumber);
    if (!line) {
        fprintf(stderr, "Failed to get line %d on %s\n", g_pinNumber, chipname);
        gpiod_chip_close(chip);
        exit(1);
    }
    printf("Got GPIO line %d\n", g_pinNumber);

    if (gpiod_line_request_both_edges_events(line, "piook") < 0) {
        perror("gpiod_line_request_both_edges_events");
        gpiod_chip_close(chip);
        exit(1);
    }
    printf("Requested events on GPIO line %d\n", g_pinNumber);

    // Event loop: wait for rising/falling edge events and forward to handler.
    printf("Starting event loop, waiting for GPIO events...\n");
    for (;;) {
        int rv = gpiod_line_event_wait(line, NULL); // wait indefinitely
        if (rv < 0) {
            perror("gpiod_line_event_wait");
            break;
        }
        if (rv == 0) {
            continue; // timeout (won't happen with NULL) but keep loop safe
        }
        struct gpiod_line_event event;
        if (gpiod_line_event_read(line, &event) == 0) {
            int highLow = (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE) ? 1 : 0;
            unsigned long timeMicros = (unsigned long)event.ts.tv_sec * 1000000UL + (unsigned long)event.ts.tv_nsec / 1000UL;
            handleEvent(highLow, timeMicros);
        }
    }

    gpiod_line_release(line);
    gpiod_chip_close(chip);
    return 0;
}

/*===========================================================
We record received 'pulses'; there are three kinds of pulse:
1 - short 'off' pulse. Represents a binary 1.
2 - long 'off' pulse. Represents a binary 0.
3 - 'on' pulse. 
0 - Represents a 'noise' pulse.
=============================================================*/
int g_bitBuffer[MAX_BIT_COUNT + 1];
int g_bitIndex = 0;

void handleEvent(int highLow, unsigned long timeMicros)
{
    // NOTE: This function was originally an ISR. With libgpiod events we are called
    // from the event loop; same logic reused but timestamp is supplied by the kernel.
    static unsigned int duration;
    static unsigned long lastTime;
    static int prevPulse = 0;

    unsigned long time = timeMicros;
    // Calc duration since last event (microseconds)
    duration = (unsigned int)(time - lastTime);
    lastTime = time;

    // Decode pulse.
    int code = decodePulse(highLow, duration);
    if(0 == code)
    {   // Noise detected.
        if(g_bitIndex != 0)
        {
            int preambleIdx = scanForPreamble();
            if(-1 != preambleIdx) {
                processSequence(preambleIdx + 4);
            }
        }

        // Reset pulseBuff.
        g_bitIndex = 0;
        prevPulse = 0;
        return;
    }

    // Note. All recorded 'off' pulses must be preceded by an 'on' pulse.
    if(3 == prevPulse)
    {
        if(3 == code)
        {   // 'On' pulse followed by another is not really possible, but if it does
            // occur then just ignore and wait for an 'off' pulse.
            return;
        }

        // 'Off' pulse received.
        if(g_bitIndex >= MAX_BIT_COUNT)
        {   // Pulse train is longer than expected. Reset buffer.
            g_bitIndex = 0;
            return;
        }

        // Buffer received bit.
        g_bitBuffer[g_bitIndex++] = code;
    }
    prevPulse = code;
}

/*====================
Pulse durations in microseconds. These were determined by examining the signal transmitted by a
ClimeMET CM7-TX, remote unit, transmitting on 433.92 MHz (temperature and humidity sensor).
ClimeMET CM9088 (Master unit)
======================*/
const unsigned int ON_PULSE_US = 1000;
const unsigned int OFF_SHORT_PULSE_US = 500;
const unsigned int OFF_LONG_PULSE_US = 1500;

// We probably need to allow for timing errors/jitter due to code runing on a non-realtime operating system.
const unsigned int JITTER_WINDOW_US = 250;
const unsigned int ON_PULSE_UPPER_US = ON_PULSE_US + JITTER_WINDOW_US;
const unsigned int ON_PULSE_LOWER_US = ON_PULSE_US - JITTER_WINDOW_US;

const unsigned int OFF_SHORT_PULSE_UPPER_US = OFF_SHORT_PULSE_US + JITTER_WINDOW_US;
const unsigned int OFF_SHORT_PULSE_LOWER_US = OFF_SHORT_PULSE_US - JITTER_WINDOW_US;

const unsigned int OFF_LONG_PULSE_UPPER_US = OFF_LONG_PULSE_US + JITTER_WINDOW_US;
const unsigned int OFF_LONG_PULSE_LOWER_US = OFF_LONG_PULSE_US - JITTER_WINDOW_US;

int decodePulse(int highLow, unsigned int duration)
{
    // Decode the pulse type based on signal level and duration
    if(0 == highLow)
    {
        // Test for short 'off' pulse.
        if(duration > OFF_SHORT_PULSE_LOWER_US && duration < OFF_SHORT_PULSE_UPPER_US) {
            return 1;
        }
        else if(duration > OFF_LONG_PULSE_LOWER_US && duration < OFF_LONG_PULSE_UPPER_US) {
            return 2;
        }
        return 0;
    }

    // Test for 'on' pulse.
    if(duration > ON_PULSE_LOWER_US && duration < ON_PULSE_UPPER_US) {
        return 3;
    }

    // Noise.
    return 0;
}

// Scan the buffered pulses for the fixed preamble sequence.
int scanForPreamble()
{
    static int preambleSeq[PREAMBLE_LEN] = PREAMBLE_SEQ;

    for(int i = 0; i < g_bitIndex - PREAMBLE_LEN; i++)
    {
        int j = 0;
        for(; j < PREAMBLE_LEN && g_bitBuffer[j + i] == preambleSeq[j]; j++)
            ;
                
        if(PREAMBLE_LEN == j) {
            return i;
        }
    }
    return -1;
}

void processSequence(int preambleIdx)
{
    // Convert the buffered bits into a byte array.
    int bitLen = g_bitIndex - preambleIdx;
    int dataLen = bitLen / 8;
    uint8_t data[EXPECTED_DATA_LEN]; // Fixed size buffer.
    int idx = preambleIdx;

    // Only process if we have the expected data length.
    if (dataLen != EXPECTED_DATA_LEN) {
        return;
    }

    for(int i = 0; i < dataLen; i++)
    {
        uint8_t b = 0;
        uint8_t mask = 0x80;

        for(int j = 0; j < 8; j++, idx++)
        {
            if(1 == g_bitBuffer[idx]) {
                b += mask;
            }
            mask = mask >> 1;
        }
        data[i] = b;
    }

    // For debugging only.
    //printHex(data, dataLen);

    // Calc checksum.
    uint8_t checksum = crc8(data, 4);
    if(checksum != data[4])
    {   // Reject.
        return;
    }

    // Parse data.
    // Temperature.
    int tempInt = ((data[1] & 0x07) << 8) + data[2];
    if(data[1] & 0x08) {
        tempInt *= -1;
    }
    float tempCelsius = tempInt * 0.1;

    // Relative humidity.
    int rh = data[3];

    // Write to file.
    if(NULL != g_outputFilename)
    {
        FILE *f = fopen(g_outputFilename, "w");
        if (!f) {
            perror("Failed to open output file");
            return;
        }
        fprintf(f, "%3.2f,%d\n", tempCelsius, rh); 
        fclose(f);
        printf("Data written to file: Temp: %4.2f, RH: %d\n", tempCelsius, rh);
    }
    else
    {
        printf("Temp: %4.2f, RH: %d\n", tempCelsius, rh);
    }
}

/*
* CRC-8 function taken from Luc Small (http://lucsmall.com), itself
* derived from the OneWire Arduino library. Modifications to
* the polynomial according to Fine Offset's CRC8 calculations.
*/
uint8_t crc8(uint8_t *addr, uint8_t len)
{
    uint8_t crc = 0;

    // Indicated changes are from reference CRC-8 function in OneWire library
    while (len--) {
        uint8_t inbyte = *addr++;
        uint8_t i;
        for (i = 8; i; i--) {
            uint8_t mix = (crc ^ inbyte) & 0x80; // changed from & 0x01
            crc <<= 1; // changed from right shift
            if (mix) crc ^= 0x31;// changed from 0x8C;
            inbyte <<= 1; // changed from right shift
        }
    }
    return crc;
}

void parseOptions(int argc, char *argv[])
{
    if(3 != argc) {
        printHelp();
        exit(1);
    }
    // Note: g_pinNumber is the GPIO line offset for gpiochip0 (kernel/BCM numbering)
    char *endptr;
    g_pinNumber = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || g_pinNumber < 0 || g_pinNumber > 53) {
        fprintf(stderr, "Invalid GPIO pin number: %s\n", argv[1]);
        exit(1);
    }
    g_outputFilename = argv[2];
}

void printHelp()
{
    printf("piook: Linux GPIO character-device (libgpiod) On-Off Keying Decoder for CliMET 433MHz weather station.\n");
    printf("Usage:\n");
    printf("  piook gpioLine outfile\n");
    printf("\n");
    printf("gpioLine: GPIO line number (kernel/BCM offset for gpiochip0) to listen on.\n");
    printf("outfile: filename to write data to.\n");
    printf("\n");
    printf(" * Must be called with privileges to access /dev/gpiochip* (usually root or gpio group).\n\n");
    printf(" * piook will listen on the specified gpio line for valid OOK sequences from the cliMET weather station.\n\n");
    printf(" * Valid sequences are decoded to a temperature in Centigrade, and a relative humidity (RH%%) value.\n\n");
    printf(" * Decoded data is written to the output file in the format: temp,RH\n\n");
    printf(" * Each update overwrites the previous file; the file will contain the most recent reading.\n\n");
    printf(" * Project URL: http://github.com/colgreen/piook\n\n");
}

/* For debugging only*/
/*
void printHex(uint8_t* buf, int len)
{
    char str[len*3];
    unsigned char* pin = buf;
    const char* hex = "0123456789ABCDEF";
    char* pout = str;
    int i = 0;
    for(; i < len - 1; ++i){
        *pout++ = hex[(*pin>>4)&0xF];
        *pout++ = hex[(*pin++)&0xF];
        *pout++ = ':';
    }
    *pout++ = hex[(*pin>>4)&0xF];
    *pout++ = hex[(*pin)&0xF];
    *pout = 0;
    printf("%s\n", str);
}
*/
