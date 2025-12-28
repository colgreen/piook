/**
 * @file piook.c
 * @brief Main program for piook OOK decoder.
 *
 * This is the main entry point for the piook application. It sets up GPIO
 * monitoring using libgpiod, processes command-line arguments, and runs
 * the event loop to decode OOK signals from a CliMET weather station.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <gpiod.h>
#include <unistd.h>
#include "piook.h"

/** GPIO pin number to monitor (kernel/BCM offset for gpiochip0) */
int g_pinNumber = 7;
/** Output filename for decoded weather data */
char* g_outputFilename;

/**
 * @brief Main entry point for the piook application.
 *
 * Initializes GPIO monitoring, sets up event handling, and runs the main
 * event loop to process OOK signals from the weather station.
 *
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return Exit status (0 on success, 1 on error)
 */
int main(int argc, char *argv[])
{
    // Parse command-line arguments to get GPIO pin and output file
    parseOptions(argc, argv);

    // Use default GPIO chip (gpiochip0)
    const char *chipname = "gpiochip0";
    struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        exit(1);
    }
    printf("Opened GPIO chip %s\n", chipname);

    // Get the requested GPIO line
    struct gpiod_line *line = gpiod_chip_get_line(chip, g_pinNumber);
    if (!line) {
        fprintf(stderr, "Failed to get line %d on %s\n", g_pinNumber, chipname);
        gpiod_chip_close(chip);
        exit(1);
    }
    printf("Got GPIO line %d\n", g_pinNumber);

    // Configure line for both rising and falling edge events
    if (gpiod_line_request_both_edges_events(line, "piook") < 0) {
        perror("gpiod_line_request_both_edges_events");
        gpiod_chip_close(chip);
        exit(1);
    }
    printf("Requested events on GPIO line %d\n", g_pinNumber);

    // Main event loop: wait for and process GPIO events
    printf("Starting event loop, waiting for GPIO events...\n");
    for (;;) {
        // Wait indefinitely for the next edge event
        int rv = gpiod_line_event_wait(line, NULL);
        if (rv < 0) {
            perror("gpiod_line_event_wait");
            break;
        }
        if (rv == 0) {
            continue; // Timeout (shouldn't happen with NULL timeout)
        }

        // Read the event details
        struct gpiod_line_event event;
        if (gpiod_line_event_read(line, &event) == 0) {
            // Convert event type to high/low signal level
            int highLow = (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE) ? 1 : 0;
            // Convert timestamp to microseconds
            unsigned long timeMicros = (unsigned long)event.ts.tv_sec * 1000000UL +
                                      (unsigned long)event.ts.tv_nsec / 1000UL;
            // Forward to decoder
            handleEvent(highLow, timeMicros);
        }
    }

    // Cleanup GPIO resources
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    return 0;
}

/**
 * @brief Parses command-line arguments.
 *
 * Expects exactly 2 arguments: GPIO pin number and output filename.
 * Validates the GPIO pin number range and stores the values in globals.
 *
 * @param argc Number of arguments
 * @param argv Argument array
 */
void parseOptions(int argc, char *argv[])
{
    // Require exactly 2 arguments: pin number and output file
    if(3 != argc) {
        printHelp();
        exit(1);
    }

    // Parse GPIO pin number (kernel/BCM offset for gpiochip0)
    char *endptr;
    g_pinNumber = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || g_pinNumber < 0 || g_pinNumber > 53) {
        fprintf(stderr, "Invalid GPIO pin number: %s\n", argv[1]);
        exit(1);
    }

    // Store output filename
    g_outputFilename = argv[2];
}

/**
 * @brief Displays usage information and help text.
 *
 * Prints detailed information about command-line usage, requirements,
 * and program functionality.
 */
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
