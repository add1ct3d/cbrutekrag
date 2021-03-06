/*
Copyright (c) 2014-2018 Jorge Matricali

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "cbrutekrag.h"
#include "bruteforce_ssh.h"
#include "log.h"
#include "str.h"
#include "wordlist.h"
#include "detection.h"
#include "progressbar.h"

int g_verbose = 0;
int g_timeout = 3;
char *g_blankpass_placeholder = "$BLANKPASS";

void print_banner()
{
    printf(
        "\033[92m           _                _       _\n"
        "          | |              | |     | |\n"
        "\033[37m      ___\033[92m | |__  _ __ _   _| |_ ___| | ___ __ __ _  __ _\n"
        "\033[37m     / __|\033[92m| '_ \\| '__| | | | __/ _ \\ |/ / '__/ _` |/ _` |\n"
        "\033[37m    | (__ \033[92m| |_) | |  | |_| | ||  __/   <| | | (_| | (_| |\n"
        "\033[37m     \\___|\033[92m|_.__/|_|   \\__,_|\\__\\___|_|\\_\\_|  \\__,_|\\__, |\n"
        "              \033[0m\033[1mOpenSSH Brute force tool 0.4.0\033[0m\033[92m        __/ |\n"
        "          \033[0m(c) Copyright 2014-2018 Jorge Matricali\033[92m  |___/\033[0m\n\n"
    );
}

void usage(const char *p)
{
    printf("\nusage: %s [-h] [-v] [-T TARGETS.lst] [-C combinations.lst]\n"
            "\t\t[-t THREADS] [-o OUTPUT.txt] [TARGETS...]\n\n", p);
}

int main(int argc, char** argv)
{
    int opt;
    int total = 0;
    int THREADS = 1;
    int PERFORM_SCAN = 0;
    char *hostnames_filename = NULL;
    char *combos_filename = NULL;
    char *output_filename = NULL;
    FILE *output = NULL;

    while ((opt = getopt(argc, argv, "T:C:t:o:svh")) != -1) {
        switch (opt) {
            case 'v':
                g_verbose = 1;
                break;
            case 'T':
                hostnames_filename = optarg;
                break;
            case 'C':
                combos_filename = optarg;
                break;
            case 't':
                THREADS = atoi(optarg);
                break;
            case 'o':
                output_filename = optarg;
                break;
            case 's':
                PERFORM_SCAN = 1;
                break;
            case 'h':
                print_banner();
                usage(argv[0]);
                printf("  -h                This help\n"
                        "  -v                Verbose mode\n"
                        "  -s                Scan mode\n"
                        "  -T <targets>      Targets file\n"
                        "  -C <combinations> Username and password file\n"
                        "  -t <threads>      Max threads\n"
                        "  -o <output>       Output log file\n");
                exit(EXIT_SUCCESS);
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    print_banner();

    /* Targets */
    wordlist_t hostnames;
    hostnames.length = 0;
    hostnames.words = NULL;

    while (optind < argc) {
        wordlist_append_range(&hostnames, argv[optind]);
        optind++;
    }
    if (hostnames.words == NULL && hostnames_filename == NULL) {
        hostnames_filename = strdup("hostnames.txt");
    }
    if (hostnames_filename != NULL) {
        wordlist_append_from_file(&hostnames, hostnames_filename);
    }

    /* Load username/password combinations */
    if (combos_filename == NULL) {
        combos_filename = strdup("combos.txt");
    }
    wordlist_t combos = wordlist_load(combos_filename);

    /* Calculate total attemps */
    total = hostnames.length * combos.length;

    printf("\nAmount of username/password combinations: %zu\n", combos.length);
    printf("Number of targets: %zu\n", hostnames.length);
    printf("Total attemps: %d\n", total);
    printf("Max threads: %d\n\n", THREADS);

    if (total == 0) {
        log_error("No work to do.");
        exit(EXIT_FAILURE);
    }

    /* Output file */
    if (output_filename != NULL) {
        output = fopen(output_filename, "a");
        if (output == NULL) {
            log_error("Error opening output file. (%s)", output_filename);
            exit(EXIT_FAILURE);
        }
    }

    /* Port scan and honeypot detection */
    if (PERFORM_SCAN) {
        printf("Starting servers discoverage process...\n\n");
        detection_start(&hostnames, &hostnames, THREADS);
        printf("\n\nNumber of targets after filtering: %zu\n", hostnames.length);
    }

    if (THREADS > hostnames.length) {
        printf("Decreasing max threads to %zu.\n", hostnames.length);
        THREADS = hostnames.length;
    }

    /* Bruteforce */
    pid_t pid = 0;
    int p = 0;
    int count = 0;

    for (int x = 0; x < combos.length; x++) {
        char **login_data = str_split(combos.words[x], ' ');
        if (login_data == NULL) {
            continue;
        }
        if (strcmp(login_data[1], g_blankpass_placeholder) == 0) {
            login_data[1] = strdup("");
        }
        for (int y = 0; y < hostnames.length; y++) {

            if (p >= THREADS){
                waitpid(-1, NULL, 0);
                p--;
            }

            log_debug(
                "HOSTNAME=%s\tUSERNAME=%s\tPASSWORD=%s",
                hostnames.words[y],
                login_data[0],
                login_data[1]
            );

            pid = fork();

            if (pid) {
                p++;
            } else if(pid == 0) {
                bruteforce_ssh_try_login(hostnames.words[y], login_data[0],
                    login_data[1], count, total, output);
                exit(EXIT_SUCCESS);
            } else {
                log_error("Fork failed!");
            }

            count++;
        }
    }

    pid = 0;

    if (output != NULL) {
        fclose(output);
    }

    if (! g_verbose) {
        progressbar_render(count, total, NULL, -1);
        printf("\f");
    }

    exit(EXIT_SUCCESS);
}
