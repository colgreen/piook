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
#include <signal.h>
#include <getopt.h>
#include "piook.h"

/** GPIO pin number to monitor (kernel/BCM offset for gpiochip0) */
int g_pinNumber = 7;
/** Output filename for decoded weather data */
char* g_outputFilename;
/** GPIO chip name to use */
const char* g_chipName = "gpiochip0";
/** Verbose output flag */
int g_verbose = 0;

/** Global flag for graceful shutdown */
volatile sig_atomic_t g_keepRunning = 1;

/**
 * @brief Signal handler for graceful shutdown.
 *
 * Sets the global flag to stop the main event loop.
 *
 * @param signum Signal number (unused)
 */
void signalHandler(int signum)
{
    (void)signum; // Suppress unused parameter warning
    g_keepRunning = 0;
}

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
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);  // Ctrl+C
    signal(SIGTERM, signalHandler); // kill command

    // Parse command-line arguments to get GPIO pin and output file
    parseOptions(argc, argv);

    // Use specified GPIO chip
    struct gpiod_chip *chip = gpiod_chip_open_by_name(g_chipName);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        exit(1);
    }
    if (g_verbose) {
        printf("Opened GPIO chip %s\n", g_chipName);
    }

    // Get the requested GPIO line
    struct gpiod_line *line = gpiod_chip_get_line(chip, g_pinNumber);
    if (!line) {
        fprintf(stderr, "Failed to get line %d on %s\n", g_pinNumber, g_chipName);
        gpiod_chip_close(chip);
        exit(1);
    }
    if (g_verbose) {
        printf("Got GPIO line %d\n", g_pinNumber);
    }

    // Configure line for both rising and falling edge events
    if (gpiod_line_request_both_edges_events(line, "piook") < 0) {
        perror("gpiod_line_request_both_edges_events");
        gpiod_line_release(line);
        gpiod_chip_close(chip);
        exit(1);
    }
    if (g_verbose) {
        printf("Requested events on GPIO line %d\n", g_pinNumber);
    }

    // Main event loop: wait for and process GPIO events
    if (g_verbose) {
        printf("Starting event loop, waiting for GPIO events...\n");
    }
    printf("Press Ctrl+C to exit\n");
    for (;;) {
        // Check if we should exit
        if (!g_keepRunning) {
            printf("\nShutting down gracefully...\n");
            break;
        }

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
    printf("GPIO resources cleaned up. Exiting.\n");
    return 0;
}

/**
 * @brief Parses command-line arguments using getopt.
 *
 * Supports both short and long options for flexible command-line interface.
 * Options include help, verbose mode, GPIO pin, output file, and GPIO chip.
 *
 * @param argc Number of arguments
 * @param argv Argument array
 */
void parseOptions(int argc, char *argv[])
{
    int opt;
    int option_index = 0;

    // Define long options
    static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"verbose", no_argument,       0, 'v'},
        {"pin",     required_argument, 0, 'p'},
        {"output",  required_argument, 0, 'o'},
        {"chip",    required_argument, 0, 'c'},
        {0,         0,                 0,  0 }
    };

    // Parse options
    while ((opt = getopt_long(argc, argv, "hvp:o:c:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printHelp();
                exit(0);
                break;
            case 'v':
                g_verbose = 1;
                break;
            case 'p':
                {
                    char *endptr;
                    long pin_val = strtol(optarg, &endptr, 10);
                    if (*endptr != '\0' || pin_val < 0 || pin_val > 63) {
                        fprintf(stderr, "Invalid GPIO pin number: %s (must be 0-63)\n", optarg);
                        exit(1);
                    }
                    g_pinNumber = (int)pin_val;
                }
                break;
            case 'o':
                if (!optarg || strlen(optarg) == 0) {
                    fprintf(stderr, "Error: Output filename cannot be empty\n");
                    exit(1);
                }
                g_outputFilename = optarg;
                break;
            case 'c':
                if (!optarg || strlen(optarg) == 0) {
                    fprintf(stderr, "Error: GPIO chip name cannot be empty\n");
                    exit(1);
                }
                g_chipName = optarg;
                break;
            case '?':
                // getopt_long already printed an error message
                printHelp();
                exit(1);
                break;
            default:
                printHelp();
                exit(1);
                break;
        }
    }

    // Check for required arguments
    if (g_outputFilename == NULL) {
        fprintf(stderr, "Error: Output filename is required (use -o or --output)\n");
        printHelp();
        exit(1);
    }

    // Check for unexpected non-option arguments
    if (optind < argc) {
        fprintf(stderr, "Error: Unexpected argument: %s\n", argv[optind]);
        printHelp();
        exit(1);
    }
}

/**
 * @brief Displays usage information and help text.
 *
 * Prints detailed information about command-line usage, requirements,
 * and program functionality with all available options.
 */
void printHelp()
{
    printf("piook: Linux GPIO character-device (libgpiod) On-Off Keying Decoder for CliMET 433MHz weather station.\n");
    printf("\nUsage:\n");
    printf("  piook [OPTIONS]\n");
    printf("\nOptions:\n");
    printf("  -h, --help                 Show this help message\n");
    printf("  -v, --verbose              Enable verbose output\n");
    printf("  -p, --pin PIN              GPIO line number (0-63, default: 7)\n");
    printf("  -o, --output FILE          Output filename (required)\n");
    printf("  -c, --chip CHIP            GPIO chip name (default: gpiochip0)\n");
    printf("\nExamples:\n");
    printf("  piook -p 17 -o weather.txt\n");
    printf("  piook --verbose --chip gpiochip1 -p 23 -o data.txt\n");
    printf("  piook -o weather.txt  (use default pin 7)\n");
    printf("\nNotes:\n");
    printf(" * Must be called with privileges to access /dev/gpiochip* (usually root or gpio group).\n");
    printf(" * piook will listen on the specified gpio line for valid OOK sequences from the cliMET weather station.\n");
    printf(" * Valid sequences are decoded to a temperature in Centigrade, and a relative humidity (RH%%) value.\n");
    printf(" * Decoded data is written to the output file in the format: temp,RH\n");
    printf(" * Each update overwrites the previous file; the file will contain the most recent reading.\n");
    printf(" * Example output: 23.45,65\n");
    printf(" * Project URL: http://github.com/colgreen/piook\n");
}
