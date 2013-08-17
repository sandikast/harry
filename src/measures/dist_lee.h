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

#ifndef DIST_LEE_H
#define DIST_LEE_H

#include "str.h"

/* Module interface */
void dist_lee_config();
float dist_lee_compare(str_t, str_t);

#endif /* DIST_LEE_H */
