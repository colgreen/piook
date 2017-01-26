#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <wiringPi.h>
#include "piook.h"

// GPIO Pin to monitor.
int _pinNum = 7;
char* _outfilename;

int main(int argc, char *argv[]) 
{
    // Parse command line options.
    parseOptions(argc, argv);

    struct timespec tim;
    tim.tv_sec = 0;
    tim.tv_nsec = 500000000L;

    // Init GPIO and wiringPi using the wiringPi 'simplified' pin numbering scheme.
    // Scheme is defined at http://wiringpi.com/pins/
    // Note. Must be called with root privileges.
    if(wiringPiSetup() == -1) 
    {   // Init failed. wiringPi writes a message so just return an error code here.
        exit(1);
    }

    // Hook-up interrupt service routine.
    wiringPiISR(_pinNum, INT_EDGE_BOTH, &handleInterrupt);

    // Main thread now sleeps.
    for(;;)
    {
        //printf("loopy");
        fflush(stdout);
        nanosleep(&tim, NULL);
    }
}

/*===========================================================
We record received 'pulses'; there are three kinds of pulse:
1 - short 'off' pulse. Represents a binary 1.
2 - long 'off' pulse. Represents a binary 0.
3 - 'on' pulse. 
0 - Represents a 'noise' pulse.
=============================================================*/
const int __maxBits = 128;
int _bitBuff[__maxBits+1];
int _bitIdx = 0;

void handleInterrupt() 
{
    // FIXME: These static variables are unsafe because the interrupt handler can get called
    // while a handler is already running (maximum of two calls at a time according to wiringPi docs).
    // Perhaps use a spinlock? 
    // Ideally I'd like to queue the work for another thread to perform, but that's beyond my 
    // C++/Linux abilities right now!
    static unsigned int duration;
    static unsigned long lastTime;
    static int prevPulse = 0;

    // Get current time and IO pin level.
    long time = micros();
    int highLow = digitalRead(_pinNum);

    // Calc duration since last interrupt.
    // TODO: Get high precision interrupt time? (i.e. recorded with the actual interrupt)
    duration = time - lastTime;
    lastTime = time;

    // ENHANCEMENT: The below logic relies on a noise pulse to trigger attempted decoding of a received message; we should attempt decode 
    // upon reception of enough bits and perhaps use a circular buffer.
    
    // Decode pulse.
    int code = decodePulse(highLow, duration);
    if(0 == code)
    {   // Noise detected.
        // If we have buffered data then now is a good time to dump it.   
        if(_bitIdx != 0)
        {
            int preambleIdx = scanForPreamble();
            if(-1 != preambleIdx) {
                processSequence(preambleIdx + 4);
            }   
        }

        // Reset pulseBuff.
        _bitIdx = 0;
        prevPulse = 0;
        return;
    }

    // All recorded 'off' pulses must be preceded by an 'on' pulse.
    if(3 == prevPulse)
    {
        if(3 == code)
        {   // 'On' pulse followed by another is not really possible, but if it does
            // occur then just ignore and wait for an 'off' pulse.
            return;
        }

        // 'Off' pulse received.
        if(_bitIdx >= __maxBits)
        {   // Pulse train is longer than expected. Reset buffer.
            _bitIdx = 0;
        }

        // Buffer received bit.
        _bitBuff[_bitIdx++] = code;
    }
    prevPulse = code;
}

/*====================
Pulse durations in microseconds. These were determined by examining the signal transmitted by a
ClimeMET CM7-TX, remote unit, transmitting on 433.92 MHz (temperature and humidity sensor).
ClimeMET CM9088 (Master unit)
======================*/
const unsigned int __onMu = 1000;
const unsigned int __offShortMu = 500;
const unsigned int __offLongMu = 1500;

// We probably need to allow for timing errors/jitter due to code runing on a non-realtime operating system.
const unsigned int __jitterWindow = 250;
const unsigned int __onMuUpper = __onMu + __jitterWindow;
const unsigned int __onMuLower = __onMu - __jitterWindow;

const unsigned int __offShortMuUpper = __offShortMu + __jitterWindow;
const unsigned int __offShortMuLower = __offShortMu - __jitterWindow;

const unsigned int __offLongMuUpper = __offLongMu + __jitterWindow;
const unsigned int __offLongMuLower = __offLongMu - __jitterWindow;

int decodePulse(int highLow, unsigned int duration)
{
    if(0 == highLow)
    {
        // Test for short 'off' pulse.
        if(duration > __offShortMuLower && duration < __offShortMuUpper) {
            return 1;
        }
        else if(duration > __offLongMuLower && duration < __offLongMuUpper) {
            return 2;
        }
        return 0;
    }

    // Test for 'on' pulse.
    if(duration > __onMuLower && duration < __onMuUpper) {
        return 3;
    }

    // Noise.
    return 0;
}

// Scan the buffered pulses for the fixed preamble sequence.
int scanForPreamble()
{
    static int preambleSeq[8] = {1, 1, 1, 1, 2, 1, 2, 2};

    for(int i=0; i<_bitIdx-8; i++)
    {
        int j=0;
        for(; j<8 && _bitBuff[j+i] == preambleSeq[j]; j++);
                
        if(8==j) {
            return i;
        }
    }
    return -1;
}

void processSequence(int preambleIdx)
{
    // Convert the buffered bits into a byte array.
    int bitLen = _bitIdx - preambleIdx;
    int dataLen = bitLen / 8;
    uint8_t data[dataLen];
    int idx = preambleIdx;

    for(int i=0; i<dataLen; i++)
    {
        uint8_t b = 0;
        uint8_t mask = 0x80;

        for(int j=0; j<8; j++, idx++)
        {
            if(1==_bitBuff[idx]) {
                b += mask;
            }
            mask = mask >> 1;
        }
        data[i] = b;
    }

    // For debugging only.
    //printHex(data, dataLen);

    // Validation.
    if(5 != dataLen)
    {   // Reject.
        return;
    }

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
    if(NULL != _outfilename)
    {
        FILE *f = fopen(_outfilename, "w");
        fprintf(f, "%3.2f,%d\n", tempCelsius, rh); 
        fclose(f);
    }
    else
    {
        printf("Temp: %4.2f, RH: %d\n", tempCelsius, rh);
    }
}

/*
* Function taken from Luc Small (http://lucsmall.com), itself
* derived from the OneWire Arduino library. Modifications to
* the polynomial according to Fine Offset's CRC8 calulations.
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
    _pinNum = atoi(argv[1]);
    _outfilename = argv[2];
}

void printHelp()
{
    printf("piook: Raspberry Pi On-Off Keying Decoder for CliMET 433MHz weather station.\n");
    printf("Usage:\n");
    printf("  piook pinNumber outfile\n");
    printf("\n");
    printf("pinNumber: GPIO pin number (wiringPi number scheme) to listen on. Based on the wiringPi 'simplified' pin numbering scheme (see defined at http://wiringpi.com/pins/)\n");
    printf("outfile: filename to write data to.\n");
    printf("\n");
    printf(" * Must be called with root privileges.\n\n");
    printf(" * piook will listen on the specified pin for valid OOK sequences from the cliMET weather station.\n\n");
    printf(" * Valid sequences are decoded to a temperature in Centigrade, and a relative humidity (RH%) value.\n\n");
    printf(" * The decoded data is written to the output file in the format: temp,RH with a newline (\\n) terminator\n\n");
    printf(" * Each update overwrites the previous file, i.e. the file will always contain a single line\n");
    printf("   containing the most recently received data.\n\n");
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
    for(; i < len-1; ++i){
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
