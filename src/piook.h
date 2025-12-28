
#include <stdint.h>

// Pulse type enumeration
enum PulseType {
    PULSE_NOISE = 0,     // A pulse that does not conform to expected timings.
    PULSE_SHORT_OFF = 1, // Represents a binary 1.
    PULSE_LONG_OFF = 2,  // Represents a binary 0.
    PULSE_ON = 3         // 'On' pulse.
};

#define MAX_BIT_COUNT 128
#define EXPECTED_DATA_LEN 5
#define PREAMBLE_LEN 8
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

// Pulse durations in microseconds
extern const unsigned int ON_PULSE_US;
extern const unsigned int OFF_SHORT_PULSE_US;
extern const unsigned int OFF_LONG_PULSE_US;
extern const unsigned int JITTER_WINDOW_US;
extern const unsigned int ON_PULSE_UPPER_US;
extern const unsigned int ON_PULSE_LOWER_US;
extern const unsigned int OFF_SHORT_PULSE_UPPER_US;
extern const unsigned int OFF_SHORT_PULSE_LOWER_US;
extern const unsigned int OFF_LONG_PULSE_UPPER_US;
extern const unsigned int OFF_LONG_PULSE_LOWER_US;

// Global variables
extern int g_pinNumber;
extern char* g_outputFilename;
extern int g_bitBuffer[MAX_BIT_COUNT + 1];
extern size_t g_bitIndex;

// Function declarations
void parseOptions(int argc, char *argv[]);
void printHelp();

void handleEvent(int highLow, unsigned long timeMicros);
enum PulseType decodePulse(int highLow, unsigned int duration);
size_t scanForPreamble();

void processSequence(size_t preambleIdx);
void printHex(uint8_t* buf, int len);
uint8_t crc8(uint8_t *addr, uint8_t len);
