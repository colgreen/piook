#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "piook.h"

// Pulse durations in microseconds
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

// Global variables for decoding
int g_bitBuffer[MAX_BIT_COUNT + 1];
size_t g_bitIndex = 0;

void handleEvent(int highLow, unsigned long timeMicros)
{
    // NOTE: This function was originally an ISR. With libgpiod events we are called
    // from the event loop; same logic reused but timestamp is supplied by the kernel.
    static unsigned int duration;
    static unsigned long lastTime;
    static enum PulseType prevPulse = PULSE_NOISE;

    unsigned long time = timeMicros;
    // Calc duration since last event (microseconds)
    duration = (unsigned int)(time - lastTime);
    lastTime = time;

    // Decode pulse.
    enum PulseType code = decodePulse(highLow, duration);
    if(PULSE_NOISE == code)
    {
        if(g_bitIndex != 0)
        {
            size_t preambleIdx = scanForPreamble();
            if(SIZE_MAX != preambleIdx) {
                processSequence((int)(preambleIdx + 4));
            }
        }

        // Reset pulseBuff.
        g_bitIndex = 0;
        prevPulse = PULSE_NOISE;
        return;
    }

    // Note. All recorded 'off' pulses must be preceded by an 'on' pulse.
    if(PULSE_ON == prevPulse)
    {
        if(PULSE_ON == code)
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

enum PulseType decodePulse(int highLow, unsigned int duration)
{
    // Decode the pulse type based on signal level and duration.
    if(0 == highLow)
    {
        // Test for short 'off' pulse.
        if(duration > OFF_SHORT_PULSE_LOWER_US && duration < OFF_SHORT_PULSE_UPPER_US) {
            return PULSE_SHORT_OFF;
        }
        else if(duration > OFF_LONG_PULSE_LOWER_US && duration < OFF_LONG_PULSE_UPPER_US) {
            return PULSE_LONG_OFF;
        }
        return PULSE_NOISE;
    }

    // Test for 'on' pulse.
    if(duration > ON_PULSE_LOWER_US && duration < ON_PULSE_UPPER_US) {
        return PULSE_ON;
    }

    // Noise.
    return PULSE_NOISE;
}

// Scan the buffered pulses for the fixed preamble sequence.
size_t scanForPreamble()
{
    static const int preambleSeq[PREAMBLE_LEN] = PREAMBLE_SEQ;

    // Ensure we have enough bits to scan for the preamble
    if (g_bitIndex < PREAMBLE_LEN) {
        return SIZE_MAX;
    }

    for(size_t i = 0; i < g_bitIndex - PREAMBLE_LEN; i++)
    {
        size_t j = 0;
        for(; j < PREAMBLE_LEN && g_bitBuffer[j + i] == preambleSeq[j]; j++)
            ;
                
        if(PREAMBLE_LEN == j) {
            return i;
        }
    }
    return SIZE_MAX;
}

void processSequence(size_t preambleIdx)
{
    // Convert the buffered bits into a byte array.
    size_t bitLen = g_bitIndex - preambleIdx;
    
    // Ensure we have a complete number of bytes (multiple of 8 bits)
    if (bitLen & 7) {
        return;
    }
    
    size_t dataLen = bitLen >> 3;
    uint8_t data[EXPECTED_DATA_LEN]; // Fixed size buffer.
    size_t idx = preambleIdx;

    // Only process if we have the expected data length.
    if (dataLen != EXPECTED_DATA_LEN) {
        return;
    }

    for(size_t i = 0; i < dataLen; i++)
    {
        uint8_t b = 0;
        uint8_t mask = 0x80;

        for(size_t j = 0; j < 8; j++, idx++)
        {
            if(PULSE_SHORT_OFF == g_bitBuffer[idx]) {
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