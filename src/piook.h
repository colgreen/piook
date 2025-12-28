
#include <stdint.h>

void parseOptions(int argc, char *argv[]);
void printHelp();

void handleEvent(int highLow, unsigned long timeMicros);
enum PulseType decodePulse(int highLow, unsigned int duration);
size_t scanForPreamble();

void processSequence(size_t preambleIdx);
void printHex(uint8_t* buf, int len);
uint8_t crc8(uint8_t *addr, uint8_t len);
