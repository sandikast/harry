/*
 * Harry - A Tool for Measuring String Similarity
 * Copyright (C) 2013-2014 Konrad Rieck (konrad@mlsec.org)
 * --
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.  This program is distributed without any
 * warranty. See the GNU General Public License for more details. 
 */

/**
 * @defgroup matrix Matrix object
 * Functions for processing similarity values in a symmetric matrix
 * @author Konrad Rieck (konrad@mlsec.org)
 * @{
 */

#include "config.h"
#include "common.h"
#include "util.h"
#include "hstring.h"
#include "murmur.h"
#include "hmatrix.h"

/* External variable */
extern int verbose;
extern int log_line;

/**
 * Initialize a matrix for similarity values
 * @param s Array of string objects
 * @param n Number of string objects 
 * @return Matrix object
 */
hmatrix_t *hmatrix_init(hstring_t *s, int n)
{
    assert(s && n >= 0);

    hmatrix_t *m = malloc(sizeof(hmatrix_t));
    if (!m) {
        error("Could not allocate matrix object");
        return NULL;
    }

    /* Set default ranges */
    m->num = n;
    m->x.i = 0, m->x.n = n;
    m->y.i = 0, m->y.n = n;
    m->triangular = TRUE;

    /* Initialized later */
    m->values = NULL;

    /* Allocate some space */
    m->labels = calloc(n, sizeof(float));
    m->srcs = calloc(n, sizeof(char *));
    if (!m->srcs || !m->labels) {
        error("Failed to initialize matrix for similarity values");
        return m;
    }

    /* Copy details from strings */
    for (int i = 0; i < n; i++) {
        m->labels[i] = s[i].label;
        m->srcs[i] = s[i].src ? strdup(s[i].src) : NULL;
    }

    return m;
}

/**
 * Parse a range string 
 * @param r Range object
 * @param str Range string, e.g. 3:14 or 2:-1 or :
 * @param n Maximum size
 * @return Range object
 */
static range_t parse_range(range_t r, char *str, int n)
{
    char *ptr, *end = NULL;
    long l;

    /* Empty string */
    if (strlen(str) == 0)
        return r;

    /* 
     * Since "1:1", "1:", ":1"  and ":" are all valid indices, sscanf 
     * won't do it and we have to stick to manual parsing :(
     */
    ptr = strchr(str, ':');
    if (!ptr) {
        error("Invalid range string '%s'.", str);
        return r;
    } else {
        /* Create split */
        *ptr = '\0';
    }

    /* Start parsing */
    l = strtol(str, &end, 10);
    if (strlen(str) == 0)
        r.i = 0;
    else if (*end == '\0')
        r.i = (int) l;
    else
        error("Could not parse range '%s:...'.", str);

    l = strtol(ptr + 1, &end, 10);
    if (strlen(ptr + 1) == 0)
        r.n = n;
    else if (*end == '\0')
        r.n = (int) l;
    else
        error("Could not parse range '...:%s'.", ptr + 1);

    /* Support negative end index */
    if (r.n < 0) {
        r.n = n + r.n;
    }

    /* Sanity checks */
    if (r.n < 0 || r.i < 0 || r.n > n || r.i > n - 1 || r.i >= r.n) {
        error("Invalid range '%s:%s'. Using default '0:%d'.", str, ptr + 1,
              n);
        r.i = 0, r.n = n;
    }

    return r;
}


/**
 * Enable splitting matrix 
 * @param m Matrix object
 * @param str Split string
 */
void hmatrix_split(hmatrix_t *m, char *str)
{
    int blocks, index, height;

    /* Empty string */
    if (strlen(str) == 0)
        return;

    /* Parse split string */
    if (sscanf(str, "%d:%d", &blocks, &index) != 2) {
        fatal("Invalid split string '%s'.", str);
        return;
    }

    height = ceil((m->y.n - m->y.i) / (float) blocks);

    /* Sanity checks with fatal error */
    if (blocks <= 0 || blocks > m->y.n - m->y.i) {
        fatal("Invalid number of blocks (%d).", blocks);
        return;
    }
    if (height <= 0 || height > m->y.n - m->y.i) {
        fatal("Block height too small (%d).", height);
        return;
    }
    if (index < 0 || index > blocks - 1) {
        fatal("Block index out of range (%d).", index);
        return;
    }

    /* Update range */
    m->y.i = m->y.i + index * height;
    if (m->y.n > m->y.i + height)
        m->y.n = m->y.i + height;
}


/**
 * Set the x range for computation
 * @param m Matrix object
 * @param x String for x range 
 */
void hmatrix_xrange(hmatrix_t *m, char *x)
{
    assert(m && x);
    m->x = parse_range(m->x, x, m->num);
}

/**
 * Set the y range for computation
 * @param m Matrix object
 * @param y String for y range 
 */
void hmatrix_yrange(hmatrix_t *m, char *y)
{
    assert(m && y);
    m->y = parse_range(m->y, y, m->num);
}

/** 
 * Allocate memory for matrix
 * @param m Matrix object
 * @return pointer to floats
 */
float *hmatrix_alloc(hmatrix_t *m)
{
    int xl, yl;

    /* Compute dimensions of matrix */
    xl = m->x.n - m->x.i;
    yl = m->y.n - m->y.i;

    if (m->x.n == m->y.n && m->x.i == m->y.i) {
        /* Symmetric matrix -> allocate triangle */
        m->triangular = TRUE;
        m->size = xl * (xl - 1) / 2 + xl;
    } else {
        /* Patrial matrix -> allocate rectangle */
        m->triangular = FALSE;
        m->size = xl * yl;
    }

    /* Allocate memory */
    m->values = calloc(sizeof(float), m->size);
    if (!m->values) {
        error("Could not allocate matrix for similarity values");
        return NULL;
    }

    return m->values;
}

/**
 * Set a value in the matrix
 * @param m Matrix object
 * @param x Coordinate x
 * @param y Coordinate y
 * @param f Value
 */
void hmatrix_set(hmatrix_t *m, int x, int y, float f)
{
    int idx, i, j;

    if (m->triangular) {
        if (x - m->x.i > y - m->y.i) {
            i = y - m->y.i, j = x - m->x.i;
        } else {
            i = x - m->x.i, j = y - m->y.i;
        }
        idx = ((j - i) + i * (m->x.n - m->x.i) - i * (i - 1) / 2);
    } else {
        idx = (x - m->x.i) + (y - m->y.i) * (m->x.n - m->x.i);
    }

    assert(idx < m->size);
    m->values[idx] = f;
}


/**
 * Get a value from the matrix
 * @param m Matrix object
 * @param x Coordinate x
 * @param y Coordinate y
 * @return f Value
 */
float hmatrix_get(hmatrix_t *m, int x, int y)
{
    int idx, i, j;

    if (m->triangular) {
        if (x - m->x.i > y - m->y.i) {
            i = y - m->y.i, j = x - m->x.i;
        } else {
            i = x - m->x.i, j = y - m->y.i;
        }
        idx = ((j - i) + i * (m->x.n - m->y.i) - i * (i - 1) / 2);
    } else {
        idx = (x - m->x.i) + (y - m->y.i) * (m->x.n - m->x.i);
    }

    assert(idx < m->size);
    return m->values[idx];
}

/**
 * Compute similarity measure and fill matrix
 * @param m Matrix object
 * @param s Array of string objects
 * @param measure Similarity measure 
 */
void hmatrix_compute(hmatrix_t *m, hstring_t *s,
                     double (*measure) (hstring_t x, hstring_t y))
{
    int k = 0;
    int step1 = floor(m->size * 0.01) + 1;
    double ts, ts1 = time_stamp(), ts2 = ts1;

    /*
     * It seems that the for-loop has to start at index 0 for OpenMP to 
     * collapse both loops. This renders it a little ugly, since hmatrix 
     * requires absolute indices.
     */
#pragma omp parallel for collapse(2) private(ts)
    for (int i = 0; i < m->x.n - m->x.i; i++) {
        for (int j = 0; j < m->y.n - m->y.i; j++) {
            int xi = i + m->x.i;
            int yi = j + m->y.i;

            /* Skip symmetric values */
            if (m->triangular && yi > xi)
                continue;

            float f = measure(s[xi], s[yi]);

            /* Set value in matrix */
            hmatrix_set(m, xi, yi, f);

#if 0
            /* Set symmetric value if in range */
            if (yi >= m->x.i && yi < m->x.n && xi >= m->y.i && xi < m->y.n)
                hmatrix_set(m, yi, xi, f);
#endif


			if (verbose || log_line)
#pragma omp critical
			{
				ts = time_stamp();

				/* Update progress bar every 100th step and 100ms */
				if (verbose && (k % step1 == 0 || ts - ts1 > 0.1)) {
					prog_bar(0, m->size, k);
					ts1 = ts;
				}

				/* Print log line every minute if enabled */
				if (log_line && ts - ts2 > 60) {
					log_print(0, m->size, k);
					ts2 = ts;
				}
            	k++;
			}
        }
    }

    if (verbose) {
        prog_bar(0, m->size, m->size);
    }

    if (log_line) {
        log_print(0, m->size, m->size);
    }
}


/**
 * Destroy a matrix of simililarity values and free its memory
 * @param m Matrix object
 */
void hmatrix_destroy(hmatrix_t *m)
{
    if (!m)
        return;

    if (m->values)
        free(m->values);
    for (int i = 0; m->srcs && i < m->num; i++)
        if (m->srcs[i])
            free(m->srcs[i]);

    if (m->srcs)
        free(m->srcs);
    if (m->labels)
        free(m->labels);

    free(m);
}

/** @} */
