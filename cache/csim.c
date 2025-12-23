/**
 * @file csim.c
 * @brief Implementation of a cache simulator.
 *
 * This cache simulator supports input cache configurations via command line
 * arguments and simulates cache hits and misses/evictions based on a given
 * memory trace file. It displays a summary of cache statistics after processing
 * the trace.
 *
 * Assignment for Cache-Lab.
 * Developed for courses 15-213/18-213/15-513
 *
 * @author Yu-Chi Pai <ypai@andrew.cmu.edu>
 */

#define _POSIX_C_SOURCE 200809L
#include "cachelab.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define LINELEN 128

/* Data types for cache */
typedef struct {
    int dirty;
    int valid;
    uint64_t tag;
    uint64_t lru_counter;
} line_t;

typedef struct {
    line_t *lines;
} set_t;

typedef struct {
    set_t *sets;
    int s;
    int E;
    int b;
    uint64_t counter;
    size_t set_count;
} cache_t;

/* Print command line help message */
static void help_message(const char *program_name) {
    fprintf(
        stderr,
        "Usage: %s [-v] -s <s> -b <b> -E <E> -t <trace>\n"
        "       %s -h\n\n"
        "   -h          Print this help message and exit\n"
        "   -v          Verbose mode: report effects of each memory operation\n"
        "   -s <s>      Number of set index bits (there are 2**s sets)\n"
        "   -b <b>      Number of block bits (there are 2**b blocks)\n"
        "   -E <E>      Number of lines per set (associativity)\n"
        "   -t <trace>  File name of the memory trace to process\n"
        "The -s, -b, -E, and -t options must be supplied for all "
        "simulations.\n",
        program_name, program_name);
}

/* Modify from https://man7.org/linux/man-pages/man3/strtol.3.html */
/* Parse a decimal unsigned long from a string,
 * Returns 0 on success, 1 on error. */
static int parse_arg(const char *str, unsigned long *result) {
    int base = 10;
    char *endptr = NULL;
    unsigned long val;

    errno = 0;
    val = strtoul(str, &endptr, base);
    if (errno == ERANGE) { // The resulting value was out of range
        perror("strtoul");
        return 1;
    }

    if (endptr == str ||
        *endptr != '\0') { // (No digits were found, include starting from "-")
                           // || (Further characters after number)
        fprintf(stderr, "Mandatory arguments missing or zero. Found: %s\n",
                str);
        return 1;
    }

    /* If we got here, strtoul() successfully parsed a number. */
    *result = val;
    return 0;
}

/* Initialize cache.
 * Returns 0 on success, 1 on error. */
static int init_cache(cache_t *cache, int s, int E, int b) {
    cache->s = s;
    cache->E = E;
    cache->b = b;
    cache->counter = 0;
    cache->set_count = 1 << s;

    cache->sets = calloc((cache->set_count), sizeof(set_t));
    if (cache->sets == NULL) {
        // fprintf(stderr, "Error: calloc failed for sets\n"); // debug message
        return 1;
    }

    for (size_t i = 0; i < cache->set_count; i++) {
        cache->sets[i].lines = calloc((size_t)E, sizeof(line_t));
        if (cache->sets[i].lines == NULL) {
            // fprintf(stderr, "Error: calloc failed for lines in set %lu\n",
            // i); // debug message
            for (size_t j = 0; j < i; j++) {
                free(cache->sets[j].lines);
            }
            free(cache->sets);
            return 1;
        }
    }
    return 0;
}

/* Free all memory allocated for the cache */
static void free_cache(cache_t *cache) {
    for (size_t i = 0; i < cache->set_count; i++) {
        free(cache->sets[i].lines);
    }
    free(cache->sets);
}

/* Search for a line in the cache with the given tag
 * If found, return the index of the line.
 * Otherwise, return -1. */
static int search_hit_line_idx(cache_t *cache, size_t set_idx, uint64_t tag) {
    set_t *set = &cache->sets[set_idx];
    for (int i = 0; i < cache->E; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            // hit
            return i;
        }
    }
    // miss
    return -1;
}

/* Search for an empty line or a victim line (LRU) in the set
 * If found, return the index of the line.
 * Otherwise, return the index of the LRU line. */
static int search_victim_or_empty_line_idx(cache_t *cache, size_t set_idx) {
    set_t *set = &cache->sets[set_idx];
    int victim_idx = 0;
    uint64_t min_lru_counter = set->lines[0].lru_counter;

    for (int i = 0; i < cache->E; i++) {
        if (set->lines[i].valid == 0) {
            // empty line
            return i;
        }
        // victim line
        if (set->lines[i].lru_counter < min_lru_counter) {
            min_lru_counter = set->lines[i].lru_counter;
            victim_idx = i;
        }
    }
    return victim_idx;
}

/* Apply a single memory operation (op == 'L' or 'S') to the cache. */
static void touch_cache(cache_t *cache, char op, uint64_t address,
                        csim_stats_t *stats) {
    cache->counter++;

    size_t set_idx = ((address >> cache->b) & ((1 << cache->s) - 1));
    uint64_t tag = address >> (cache->s + cache->b);

    int hit_line_idx = search_hit_line_idx(cache, set_idx, tag);
    // hit
    if (hit_line_idx != -1) {
        stats->hits++;
        cache->sets[set_idx].lines[hit_line_idx].lru_counter = cache->counter;

        if (op == 'S') {
            if (cache->sets[set_idx].lines[hit_line_idx].dirty == 0) {
                // clean -> dirty
                stats->dirty_bytes += (1 << cache->b);
                cache->sets[set_idx].lines[hit_line_idx].dirty = 1;
            }
        }
        return;
    }

    // miss
    stats->misses++;
    int line_idx = search_victim_or_empty_line_idx(cache, set_idx);
    line_t *line = &cache->sets[set_idx].lines[line_idx];

    // eviction
    if (line->valid) {
        stats->evictions++;
        if (line->dirty) {
            stats->dirty_evictions += (1 << cache->b);
            stats->dirty_bytes -= (1 << cache->b);
        }
    }

    // fill the line
    line->valid = 1;
    line->tag = tag;
    line->lru_counter = cache->counter;
    if (op == 'S') {
        line->dirty = 1;
        stats->dirty_bytes += (1 << cache->b);
    } else {
        line->dirty = 0;
    }
}

/* Parse a single line of the trace file
 * Return 0 on success, 1 on failure.
 * Assign file inline values to op_out, addr_out, size_out.
 */
static int parse_trace_line(const char *linebuf, char *op_out,
                            uint64_t *addr_out, unsigned long *size_out) {
    if (linebuf == NULL) {
        return 1;
    }

    size_t linelen = strlen(linebuf);
    if (linelen == 0 || linebuf[linelen - 1] != '\n') { // empty || too long
        return 1;
    }

    const char *p = linebuf;
    char op = *p;
    if (op != 'L' && op != 'S') {
        return 1;
    }
    p += 2; // skip op and following space

    char *endptr = NULL;
    errno = 0;
    unsigned long long address = strtoull(p, &endptr, 16);
    if (errno == ERANGE || endptr == p) {
        return 1;
    }

    p = endptr;
    p++; // skip comma

    endptr = NULL;
    errno = 0;
    unsigned long size = strtoul(p, &endptr, 10);
    if (errno == ERANGE || endptr == p || size == 0) {
        return 1;
    }

    *op_out = op;
    *addr_out = (uint64_t)address;
    *size_out = size;
    return 0;
}

/* Process a trace file
 * For each line, parse it and touch the cache accordingly.
 * Return number of parse_error.
 */
int process_trace_file(const char *trace_file, cache_t *cache,
                       csim_stats_t *stats) {
    FILE *tfp = fopen(trace_file, "rt");
    if (!tfp) {
        // fprintf(stderr, "Error opening trace file '%s': %s\n",
        //         trace_file, strerror(errno));
        return 1;
    }

    char linebuf[LINELEN];
    int parse_error = 0;
    unsigned long line_num = 0;

    while (fgets(linebuf, LINELEN, tfp)) {
        line_num++;

        char op;
        uint64_t address;
        unsigned long size;

        if (parse_trace_line(linebuf, &op, &address, &size) != 0) {
            parse_error++;
            continue;
        }

        // Parse the line of text in 'linebuf'.
        // printf("Line %lu: op=%c, address=%llx, size=%lu\n",
        //        line_num, op, address, size);
        touch_cache(cache, op, (uint64_t)address, stats);
    }
    fclose(tfp);
    return parse_error;
}

/* Main function
 * 1. Parse command line arguments
 * 2. Initialize cache simulator
 * 3. Process trace file
 * 4. Free cache resources
 * 5. Print summary statistics
 */
int main(int argc, char **argv) {
    int opt;
    int verbose = 0;
    unsigned long s;
    unsigned long E;
    unsigned long b;

    int s_not_exist = 1;
    int b_not_exist = 1;
    int E_not_exist = 1;
    char *trace_file = NULL;

    while ((opt = getopt(argc, argv, "vhs:b:E:t:")) != -1) {
        switch (opt) {
        case 's':
            if (parse_arg(optarg, &s)) { // error happen
                help_message(argv[0]);
                exit(EXIT_FAILURE);
            } else {
                s_not_exist = 0;
            };
            break;

        case 'b':
            if (parse_arg(optarg, &b)) {
                help_message(argv[0]);
                exit(EXIT_FAILURE);
            } else {
                b_not_exist = 0;
            };
            break;

        case 'E':
            if (parse_arg(optarg, &E)) {
                help_message(argv[0]);
                exit(EXIT_FAILURE);
            } else {
                E_not_exist = 0;
            };
            break;

        case 't':
            if (optarg) {
                trace_file = optarg;
            };
            break;

        case 'v':
            verbose = 1;
            break;

        case 'h':
            help_message(argv[0]);
            exit(EXIT_SUCCESS);

        default:
            fprintf(stderr, "Error while parsing arguments.\n");
            help_message(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Extra arguments passed.\n");
        help_message(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (s_not_exist || b_not_exist || E_not_exist || !trace_file) {
        fprintf(stderr, "Mandatory arguments missing or zero.\n");
        help_message(argv[0]);
        exit(EXIT_FAILURE);
    }

    // debug: print all the arguments
    // printf("s = %lu, b = %lu, E = %lu, trace_file = %s\n", s, b, E,
    // trace_file);

    if (s + b > 64 || s < 0 || b < 0 || E <= 0) {
        fprintf(stderr, "Error: s + b is too large (s = %lu, b = %lu)\n", s, b);
        exit(EXIT_FAILURE);
    }

    if (verbose)
        fprintf(stderr, "verbose mode on\n");

    // start simulating cache here
    csim_stats_t stats = {0, 0, 0, 0, 0};
    cache_t cache;
    if (init_cache(&cache, (int)s, (int)E, (int)b)) {
        exit(EXIT_FAILURE);
    }

    // process the trace file
    int error = process_trace_file(trace_file, &cache, &stats);
    free_cache(&cache);

    if (error) {
        fprintf(stderr, "Error processing trace file: %s\n", trace_file);
        exit(EXIT_FAILURE);
    }

    // print cache statistics
    printSummary(&stats);

    return 0;
}
