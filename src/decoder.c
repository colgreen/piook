/**
 * @file decoder.c
 * @brief OOK signal decoding implementation for CliMET weather station.
 *
 * This module handles the decoding of On-Off Keying (OOK) signals received
 * from a CliMET 433MHz weather station. It processes GPIO events, decodes
 * pulse timings, and extracts temperature and humidity data.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "piook.h"

/**
 * @brief Pulse durations in microseconds.
 *
 * These values were determined by analyzing the signal transmitted by a
 * CliMET CM7-TX remote unit operating on 433.92 MHz.
 */
const unsigned int ON_PULSE_US = 1000;
const unsigned int OFF_SHORT_PULSE_US = 500;
const unsigned int OFF_LONG_PULSE_US = 1500;

/**
 * @brief Jitter tolerance for pulse timing.
 *
 * Allows for timing variations due to running on a non-realtime operating system.
 * A 250us window provides reasonable tolerance for system scheduling delays.
 */
const unsigned int JITTER_WINDOW_US = 250;

/* Calculated timing bounds including jitter tolerance */
const unsigned int ON_PULSE_UPPER_US = ON_PULSE_US + JITTER_WINDOW_US;
const unsigned int ON_PULSE_LOWER_US = ON_PULSE_US - JITTER_WINDOW_US;

const unsigned int OFF_SHORT_PULSE_UPPER_US = OFF_SHORT_PULSE_US + JITTER_WINDOW_US;
const unsigned int OFF_SHORT_PULSE_LOWER_US = OFF_SHORT_PULSE_US - JITTER_WINDOW_US;

const unsigned int OFF_LONG_PULSE_UPPER_US = OFF_LONG_PULSE_US + JITTER_WINDOW_US;
const unsigned int OFF_LONG_PULSE_LOWER_US = OFF_LONG_PULSE_US - JITTER_WINDOW_US;

/**
 * @brief Global buffer for storing decoded pulse types during signal processing.
 *
 * This buffer accumulates pulse types as they are decoded from GPIO events.
 * When a valid preamble is detected, the buffer contents are processed to extract data.
 */
int g_bitBuffer[MAX_BIT_COUNT + 1];

/**
 * @brief Current index in the bit buffer.
 *
 * Tracks how many pulses have been stored in g_bitBuffer. Reset when noise
 * is detected or when processing a complete sequence.
 */
size_t g_bitIndex = 0;

/**
 * @brief Handles GPIO edge events and processes OOK signal decoding.
 *
 * This function is called for each rising/falling edge detected on the GPIO pin.
 * It calculates pulse durations, decodes pulse types, and manages the bit buffer.
 * When noise is detected, it attempts to scan for a valid preamble and process
 * any complete data sequences.
 *
 * @param highLow Signal level: 1 for rising edge (high), 0 for falling edge (low)
 * @param timeMicros Timestamp of the event in microseconds
 *
 * @note Originally designed as an ISR, now called from the libgpiod event loop.
 */
void handleEvent(int highLow, unsigned long timeMicros)
{
    // Static variables maintain state between function calls
    static unsigned int duration;        // Duration of current pulse
    static unsigned long lastTime;       // Timestamp of previous event
    static enum PulseType prevPulse = PULSE_NOISE;  // Previous pulse type

    unsigned long time = timeMicros;
    // Calculate duration since last event (microseconds)
    duration = (unsigned int)(time - lastTime);
    lastTime = time;

    // Filter out very short pulses that are likely noise
    // Minimum valid pulse duration is 250us (accounting for jitter tolerance)
    if(duration < 250) {
        return; // Ignore noise pulses
    }

    // Decode the current pulse type based on signal level and duration
    enum PulseType code = decodePulse(highLow, duration);

    // If noise detected, check for valid preamble in buffered data
    if(PULSE_NOISE == code)
    {
        if(g_bitIndex != 0)
        {
            size_t preambleIdx = scanForPreamble();
            if(SIZE_MAX != preambleIdx) {
                // Found preamble, process the data sequence starting after preamble
                processSequence((int)(preambleIdx + 4));
            }
        }

        // Reset buffer and state for next signal
        g_bitIndex = 0;
        prevPulse = PULSE_NOISE;
        return;
    }

    // Valid pulse detected - ensure it follows protocol rules
    // All recorded 'off' pulses must be preceded by an 'on' pulse
    if(PULSE_ON == prevPulse)
    {
        // Handle case where 'on' pulse is followed by another 'on' (invalid)
        if(PULSE_ON == code)
        {
            // Ignore and wait for an 'off' pulse
            return;
        }

        // 'Off' pulse received after 'on' pulse
        // Check buffer bounds to prevent overflow
        if(g_bitIndex >= MAX_BIT_COUNT)
        {
            // Pulse train too long - reset buffer
            g_bitIndex = 0;
            return;
        }

        // Store the pulse type in buffer
        g_bitBuffer[g_bitIndex++] = code;
    }

    // Update previous pulse state
    prevPulse = code;
}

/**
 * @brief Decodes a pulse into its type based on signal level and duration.
 *
 * Analyzes the GPIO signal level and pulse duration to classify the pulse
 * as one of the expected types: on pulse, short off pulse (binary 1),
 * long off pulse (binary 0), or noise.
 *
 * @param highLow Signal level: 1 for high/rising, 0 for low/falling
 * @param duration Pulse duration in microseconds
 * @return The decoded pulse type
 */
enum PulseType decodePulse(int highLow, unsigned int duration)
{
    // Process falling edge (low signal) - this indicates 'off' pulses
    if(0 == highLow)
    {
        // Check for short 'off' pulse (represents binary 1)
        if(duration >= OFF_SHORT_PULSE_LOWER_US && duration <= OFF_SHORT_PULSE_UPPER_US) {
            return PULSE_SHORT_OFF;
        }
        // Check for long 'off' pulse (represents binary 0)
        else if(duration >= OFF_LONG_PULSE_LOWER_US && duration <= OFF_LONG_PULSE_UPPER_US) {
            return PULSE_LONG_OFF;
        }
        // Duration doesn't match expected ranges
        return PULSE_NOISE;
    }

    // Process rising edge (high signal) - this indicates 'on' pulses
    if(duration >= ON_PULSE_LOWER_US && duration <= ON_PULSE_UPPER_US) {
        return PULSE_ON;
    }

    // Duration doesn't match expected 'on' pulse range
    return PULSE_NOISE;
}

/**
 * @brief Scans the bit buffer for the expected preamble sequence.
 *
 * Searches through the accumulated pulse types in g_bitBuffer to find the
 * fixed preamble pattern that marks the beginning of a valid data packet.
 * The preamble helps synchronize with the transmitter's data stream.
 *
 * @return Index of the first pulse in the preamble, or SIZE_MAX if not found
 */
size_t scanForPreamble()
{
    // Define the expected preamble sequence
    static const int preambleSeq[PREAMBLE_LEN] = PREAMBLE_SEQ;

    // Ensure we have enough pulses buffered to contain the preamble
    if (g_bitIndex < PREAMBLE_LEN) {
        return SIZE_MAX;
    }

    // Calculate maximum starting position to avoid buffer overflow
    const size_t maxStart = g_bitIndex - PREAMBLE_LEN;

    // Scan through the buffer looking for the preamble pattern
    // Optimized: start from most recent data (higher chance of finding preamble)
    for(size_t i = 0; i <= maxStart; i++)
    {
        // Quick check: first element must match
        if (g_bitBuffer[i] != preambleSeq[0]) {
            continue;
        }

        // Check remaining elements
        size_t j = 1;
        for(; j < PREAMBLE_LEN; j++) {
            if (g_bitBuffer[i + j] != preambleSeq[j]) {
                break;
            }
        }

        // If all preamble elements matched, return starting index
        if(j == PREAMBLE_LEN) {
            return i;
        }
    }

    // Preamble not found
    return SIZE_MAX;
}

/**
 * @brief Processes a complete data sequence starting after the preamble.
 *
 * Converts the buffered pulse types to binary data, validates the checksum,
 * and extracts temperature and humidity values. If valid, writes the data
 * to the output file and/or console.
 *
 * @param preambleIdx Index in buffer where preamble starts (data follows 4 pulses later)
 */
void processSequence(size_t preambleIdx)
{
    // Calculate total bits in the sequence after preamble
    size_t bitLen = g_bitIndex - preambleIdx;

    // Ensure we have a complete number of bytes (8 bits per byte)
    if (bitLen & 7) {
        return;  // Incomplete byte, reject
    }

    size_t dataLen = bitLen >> 3;  // Convert bits to bytes
    uint8_t data[EXPECTED_DATA_LEN]; // Fixed-size buffer for expected data
    size_t idx = preambleIdx;

    // Only process if we have the expected number of data bytes
    if (dataLen != EXPECTED_DATA_LEN) {
        return;
    }

    // Convert pulse types to binary data (8 pulses = 1 byte)
    for(size_t i = 0; i < dataLen; i++)
    {
        uint8_t b = 0;

        // Process 8 pulses to form one byte
        // PULSE_SHORT_OFF = 1 (binary 1), PULSE_LONG_OFF = 2 (binary 0)
        for(int bit = 7; bit >= 0; bit--)
        {
            b |= (g_bitBuffer[idx++] == PULSE_SHORT_OFF) ? (1 << bit) : 0;
        }

        data[i] = b;
    }

    // Optional: Debug output of raw bytes
    //printHex(data, dataLen);

    // Validate data integrity using CRC-8 checksum
    uint8_t checksum = crc8(data, 4);
    if(checksum != data[4])
    {
        return;  // Checksum mismatch, reject data
    }

    // Extract temperature from bytes 1-2
    int tempInt = ((data[1] & 0x07) << 8) + data[2];
    // Check sign bit (bit 3 of byte 1)
    if(data[1] & 0x08) {
        tempInt *= -1;  // Negative temperature
    }
    float tempCelsius = tempInt * 0.1f;  // Convert to Celsius

    // Extract relative humidity from byte 3
    int rh = data[3];

    // Output the decoded weather data
    if(NULL != g_outputFilename)
    {
        // Write to specified output file
        FILE *f = fopen(g_outputFilename, "w");
        if (!f) {
            perror("Failed to open output file");
            return;
        }
        fprintf(f, "%3.2f,%d\n", tempCelsius, rh);
        fclose(f);
        printf("Data written to file: Temp: %4.2f°C, RH: %d%%\n", tempCelsius, rh);
    }
    else
    {
        // No output file specified, print to console
        printf("Temp: %4.2f°C, RH: %d%%\n", tempCelsius, rh);
    }
}

/**
 * @brief Calculates CRC-8 checksum for data validation.
 *
 * Function taken from Luc Small (http://lucsmall.com), itself derived from
 * the OneWire Arduino library. Modifications to the polynomial according to
 * Fine Offset's CRC8 calculations used by CliMET.
 *
 * @param data Pointer to data buffer
 * @param len Length of data in bytes
 * @return Calculated CRC-8 checksum
 *
 * @note Based on Luc Small's work (http://lucsmall.com), derived from OneWire library.
 *       Polynomial modified for Fine Offset compatibility.
 */
uint8_t crc8(const uint8_t *data, uint8_t len)
{
    static const uint8_t POLY = 0x31;  // x^8 + x^5 + x^4 + 1 (CRC-8/Dallas uses 0x31 in normal form)
    uint8_t crc = 0;

    for (; len > 0; --len) {
        uint8_t byte = *data++;

        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint8_t msb_diff = (crc ^ byte) & 0x80;  // compare MSB
            crc <<= 1;
            if (msb_diff) {
                crc ^= POLY;
            }
            byte <<= 1;
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