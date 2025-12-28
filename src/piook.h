
#include <stdint.h>

void parseOptions(int argc, char *argv[]);
void printHelp();

void handleEvent(int highLow, unsigned long timeMicros);
int decodePulse(int highLow, unsigned int duration);
int scanForPreamble();

void processSequence(int preambleIdx);
void printHex(uint8_t* buf, int len);
uint8_t crc8(uint8_t *addr, uint8_t len);
