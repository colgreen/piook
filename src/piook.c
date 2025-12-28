#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <gpiod.h>
#include <unistd.h>
#include "piook.h"

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
