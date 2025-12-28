/**
 * @file piook.h
 * @brief Header file for piook OOK decoder project.
 *
 * This header defines constants, types, and function prototypes for decoding
 * On-Off Keying (OOK) signals from a CliMET 433MHz weather station using libgpiod.
 */

#include <stdint.h>

/** Version information */
#define PIOOK_VERSION "1.0.0"
#define PIOOK_DESCRIPTION "Linux GPIO character-device (libgpiod) On-Off Keying Decoder for CliMET 433MHz weather station"

/**
 * @enum PulseType
 * @brief Enumeration of different pulse types in the OOK signal.
 *
 * The CliMET weather station transmits data using OOK modulation with specific
 * pulse durations representing different bit values.
 */
enum PulseType {
    PULSE_NOISE = 0,     /**< A pulse that does not conform to expected timings. */
    PULSE_SHORT_OFF = 1, /**< Short 'off' pulse representing a binary 1. */
    PULSE_LONG_OFF = 2,  /**< Long 'off' pulse representing a binary 0. */
    PULSE_ON = 3         /**< 'On' pulse that precedes data bits. */
};

/** Maximum number of bits to buffer during decoding */
#define MAX_BIT_COUNT 128
/** Expected length of decoded data in bytes */
#define EXPECTED_DATA_LEN 5
/** Length of the preamble sequence in pulses */
#define PREAMBLE_LEN 8
/** Preamble sequence that marks the start of a valid data packet */
#define PREAMBLE_SEQ { \
    PULSE_SHORT_OFF, \
    PULSE_SHORT_OFF, \
    PULSE_SHORT_OFF, \
    PULSE_SHORT_OFF, \
    PULSE_LONG_OFF, \
    PULSE_SHORT_OFF, \
    PULSE_LONG_OFF, \
    PULSE_LONG_OFF \
}

/* Pulse durations in microseconds - determined from CliMET CM7-TX signal analysis */
extern const unsigned int ON_PULSE_US;           /**< Duration of 'on' pulse */
extern const unsigned int OFF_SHORT_PULSE_US;    /**< Duration of short 'off' pulse (binary 1) */
extern const unsigned int OFF_LONG_PULSE_US;     /**< Duration of long 'off' pulse (binary 0) */

/* Jitter tolerance for pulse timing due to non-realtime OS */
extern const unsigned int JITTER_WINDOW_US;      /**< Allowed timing variation in microseconds */

/* Calculated timing bounds including jitter tolerance */
extern const unsigned int ON_PULSE_UPPER_US;
extern const unsigned int ON_PULSE_LOWER_US;
extern const unsigned int OFF_SHORT_PULSE_UPPER_US;
extern const unsigned int OFF_SHORT_PULSE_LOWER_US;
extern const unsigned int OFF_LONG_PULSE_UPPER_US;
extern const unsigned int OFF_LONG_PULSE_LOWER_US;

/* Global variables shared across modules */
extern int g_pinNumber;                    /**< GPIO pin number to monitor */
extern char* g_outputFilename;             /**< Output filename for decoded data */
extern int g_bitBuffer[MAX_BIT_COUNT + 1]; /**< Buffer for storing decoded pulse types */
extern size_t g_bitIndex;                  /**< Current index in the bit buffer */

/* Function declarations */

/* Main program functions */
void parseOptions(int argc, char *argv[]);
void printHelp();

/* OOK decoding functions */
void handleEvent(int highLow, unsigned long timeMicros);
enum PulseType decodePulse(int highLow, unsigned int duration);
size_t scanForPreamble();
void processSequence(size_t preambleIdx);
uint8_t crc8(const uint8_t *data, uint8_t len);
