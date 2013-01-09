/*
 * Harry - A Tool for Measuring String Similarity
 * Copyright (C) 2013 Konrad Rieck (konrad@mlsec.org)
 * --
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.  This program is distributed without any
 * warranty. See the GNU General Public License for more details. 
 */

/**
 * @defgroup string String functions
 * Functions for processing strings and sequences
 * @author Konrad Rieck (konrad@mlsec.org)
 * @{
 */

#include "config.h"
#include "common.h"
#include "util.h"
#include "str.h"
#include "murmur.h"

/* External variable */
extern int verbose;

/* Global delimiter table */
char delim[256] = { DELIM_NOT_INIT };

/**
 * Free memory of the string structure
 * @param x string structure
 */
void str_free(str_t x)
{
    if (x.type == TYPE_CHAR && x.str.c)
        free(x.str.c);
    if (x.type == TYPE_SYM && x.str.s)
        free(x.str.s);
    if (x.src)
        free(x.src);
}

/** 
 * Check whether delimiters have been set
 * @return true if delimiters have been set
 */
int str_has_delim() 
{
    return (delim[0] != DELIM_NOT_INIT);
}

/** 
 * Print string structure
 * @param x string structure
 * @param p prefix for printf
 */
void str_print(str_t x, char *p)
{
    int i;

    printf("%s \t (len:%d; idx:%ld; src:%s)\n", p, x.len, x.idx, x.src);

    if (x.type == TYPE_CHAR && x.str.c) {
        printf("  str:");
        for (i = 0; i < x.len; i++)
            if (isprint(x.str.c[i]))
                printf("%c", x.str.c[i]);
            else
                printf("%%%.2x", x.str.c[i]);
        printf("\n");
    }

    if (x.type == TYPE_SYM && x.str.s) {
        printf("  sym:");
        for (i = 0; i < x.len; i++)
            printf("%d ", x.str.s[i]);
        printf("\n");
    }
}

/**
 * Decodes a string containing delimiters to a lookup table
 * @param s String containing delimiters
 */
void str_delim_set(const char *s)
{
    char buf[5] = "0x00";
    unsigned int i, j;

    if (strlen(s) == 0) {
        str_delim_reset();
        return;
    }

    memset(delim, 0, 256);
    for (i = 0; i < strlen(s); i++) {
        if (s[i] != '%') {
            delim[(unsigned int) s[i]] = 1;
            continue;
        }

        /* Skip truncated sequence */
        if (strlen(s) - i < 2)
            break;

        buf[2] = s[++i];
        buf[3] = s[++i];
        sscanf(buf, "%x", (unsigned int *) &j);
        delim[j] = 1;
    }
}

/**
 * Resets delimiters table. There is a global table of delimiter 
 * symbols which is only initialized once the first sequence is 
 * processed. This functions is used to trigger a re-initialization.
 */
void str_delim_reset()
{
    delim[0] = DELIM_NOT_INIT;
}

/**
 * Converts a string into a sequence of symbols  (words) using delimiter
 * characters.  The original character string is lost.
 * @param x character string
 * @return symbolized string
 */
str_t str_symbolize(str_t x)
{
    int i = 0, j = 0, k = 0, dlm = 0;
    int wstart = 0;

    /* A string of n chars can have at most n/2 + 1 words */
    sym_t *sym = malloc((x.len/2 + 1) * sizeof(sym_t));
    if (!sym) {
        error("Failed to allocate memory for symbols");
        return x;
    }

    /* Find first delimiter symbol */
    for (dlm = 0; !delim[(unsigned char) dlm] && dlm < 256; dlm++);

    /* Remove redundant delimiters */
    for (i = 0, j = 0; i < x.len; i++) {
        if (delim[(unsigned char) x.str.c[i]]) {
            if (j == 0 || delim[(unsigned char) x.str.c[j - 1]])
                continue;
            x.str.c[j++] = (char) dlm;
        } else {
            x.str.c[j++] = x.str.c[i];
        }
    }

    /* Extract words */
    for (wstart = i = 0; i < j + 1; i++) {
        /* Check for delimiters and remember start position */
        if ((i == j || x.str.c[i] == dlm) && i - wstart > 0) {
            uint64_t hash = hash_str(x.str.c + wstart, i - wstart);
            sym[k++] = (sym_t) hash;
            wstart = i + 1;
        }
    }
    x.len = k;
    sym = realloc(sym, x.len * sizeof(sym_t));

    /* Change representation */
    free(x.str.c);
    x.str.s = sym;
    x.type = TYPE_SYM;

    return x;
}

/**
 * Convert a c-style string to a string structure. New memory is allocated
 * and the string is copied.
 * @param x string structure
 * @param s c-style string
 */
str_t str_convert(str_t x, char *s)
{
    x.str.c = strdup(s);
    x.type = TYPE_CHAR;
    x.len = strlen(s);
    x.idx = 0;
    x.src = NULL;

    return x;
}

/**
 * Compute a 64-bit hash for a string. The hash is used at different locations.
 * Collisions are possible but not very likely (hopefully)  
 * @param x String to hash
 * @return hash value
 */
uint64_t str_hash1(str_t x)
{
    if (x.type == TYPE_CHAR && x.str.c)
        return MurmurHash64B(x.str.c, sizeof(char) * x.len, 0xc0ffee);
    if (x.type == TYPE_SYM && x.str.s)
        return MurmurHash64B(x.str.s, sizeof(sym_t) * x.len, 0xc0ffee);

    warning("Nothing to hash. String is missing");
    return 0;
}

/**
 * Compute a 64-bit hash for two strings. The computation is symmetric, that is,
 * the same strings retrieve the same hash independent of their order. 
 * Collisions are possible but not very likely (hopefully)  
 * @param x String to hash
 * @param y String to hash 
 * @return hash value
 */
uint64_t str_hash2(str_t x, str_t y)
{
    uint64_t a, b;

    if (x.type == TYPE_CHAR && y.type == TYPE_CHAR && x.str.c && y.str.c) {
        a = MurmurHash64B(x.str.c, sizeof(char) * x.len, 0xc0ffee);
        b = MurmurHash64B(y.str.c, sizeof(char) * y.len, 0xc0ffee);
        return a ^ b;
    }
    if (x.type == TYPE_SYM && y.type == TYPE_SYM && x.str.s && y.str.s) {
        a = MurmurHash64B(x.str.s, sizeof(sym_t) * x.len, 0xc0ffee);
        b = MurmurHash64B(y.str.s, sizeof(sym_t) * y.len, 0xc0ffee);
        return a ^ b;
    }

    warning("Nothing to hash. Strings are missing or incompatible.");
    return 0;
}


/** @} */
