// SPDX-License-Identifier: MIT
/**
 *
 * MIT License
 *
 * Copyright (c) dafo123
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// sit13.c
// Streaming-capable implementation of SIT method 13 (LZSS + Huffman).

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "munbox.h"
#include "sit.h"

// LZSS with a 64K sliding window, two different prefix free code sets for
// a combination of literals and lengths, and a third prefix free code set
// for the offsets. Code trees can either be stored in the datastream or
// selected out of 5 pre-defined sets.

#define MAX_CODE      321
#define INVALID_VALUE (MAX_CODE + 1)

// Longest code word that fits the 32-bit canonical code used to build trees
#define MAX_CODE_LENGTH 32

/* Disable missing-braces warning for large generated initializer tables */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif
static const int8_t first_tree_lengths[5][MAX_CODE] = {
    4,  5,  7,  8,  8,  9,  9,  9,  9,  7,  9,  9,  9,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  10, 9,  9,  10, 10, 9,
    10, 9,  9,  5,  9,  9,  9,  9,  10, 9,  9,  9,  9,  9,  9,  9,  9,  7,  9,  9,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  8,  9,  9,  8,  8,  9,  9,  9,  9,  9,  9,  9,  7,  8,  9,  7,  9,  9,  7,  7,  9,  9,
    9,  9,  10, 9,  10, 10, 10, 9,  9,  9,  5,  9,  8,  7,  5,  9,  8,  8,  7,  9,  9,  8,  8,  5,  5,  7,  10, 5,  8,
    5,  8,  9,  9,  9,  9,  9,  10, 9,  9,  10, 9,  9,  10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 9,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 9,  9,  10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 9,  9,  10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 9,  10, 9,  5,  6,  5,  5,  8,  9,
    9,  9,  9,  9,  9,  10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 9,  10, 9,  9,  9,  10, 9,  10, 9,  10, 9,  10, 9,  10, 10, 10, 9,  10, 9,  10, 10, 9,  9,  9,  6,  9,  9,  10,
    9,  5,  4,  7,  7,  8,  7,  8,  8,  8,  8,  7,  8,  7,  8,  7,  9,  8,  8,  8,  9,  9,  9,  9,  10, 10, 9,  10, 10,
    10, 10, 10, 9,  9,  5,  9,  8,  9,  9,  11, 10, 9,  8,  9,  9,  9,  8,  9,  7,  8,  8,  8,  9,  9,  9,  9,  9,  10,
    9,  9,  9,  10, 9,  9,  10, 9,  8,  8,  7,  7,  7,  8,  8,  9,  8,  8,  9,  9,  8,  8,  7,  8,  7,  10, 8,  7,  7,
    9,  9,  9,  9,  10, 10, 11, 11, 11, 10, 9,  8,  6,  8,  7,  7,  5,  7,  7,  7,  6,  9,  8,  6,  7,  6,  6,  7,  9,
    6,  6,  6,  7,  8,  8,  8,  8,  9,  10, 9,  10, 9,  9,  8,  9,  10, 10, 9,  10, 10, 9,  9,  10, 10, 10, 10, 10, 10,
    10, 9,  10, 10, 11, 10, 10, 10, 10, 10, 10, 10, 11, 10, 11, 10, 10, 9,  11, 10, 10, 10, 10, 10, 10, 9,  9,  10, 11,
    10, 11, 10, 11, 10, 12, 10, 11, 10, 12, 11, 12, 10, 12, 10, 11, 10, 11, 11, 11, 9,  10, 11, 11, 11, 12, 12, 10, 10,
    10, 11, 11, 10, 11, 10, 10, 9,  11, 10, 11, 10, 11, 11, 11, 10, 11, 11, 12, 11, 11, 10, 10, 10, 11, 10, 10, 11, 11,
    12, 10, 10, 11, 11, 12, 11, 11, 10, 11, 9,  12, 10, 11, 11, 11, 10, 11, 10, 11, 10, 11, 9,  10, 9,  7,  3,  5,  6,
    6,  7,  7,  8,  8,  8,  9,  9,  9,  11, 10, 10, 10, 12, 13, 11, 12, 12, 11, 13, 12, 12, 11, 12, 12, 13, 12, 14, 13,
    14, 13, 15, 13, 14, 15, 15, 14, 13, 15, 15, 14, 15, 14, 15, 15, 14, 15, 13, 13, 14, 15, 15, 14, 14, 16, 16, 15, 15,
    15, 12, 15, 10, 6,  6,  6,  6,  6,  9,  8,  8,  4,  9,  8,  9,  8,  9,  9,  9,  8,  9,  9,  10, 8,  10, 10, 10, 9,
    10, 10, 10, 9,  10, 10, 9,  9,  9,  8,  10, 9,  10, 9,  10, 9,  10, 9,  10, 9,  9,  8,  9,  8,  9,  9,  9,  10, 10,
    10, 10, 9,  9,  9,  10, 9,  10, 9,  9,  7,  8,  8,  9,  8,  9,  9,  9,  8,  9,  9,  10, 9,  9,  8,  9,  8,  9,  8,
    8,  8,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 9,  8,  8,  9,  8,  9,  7,  8,  8,  9,  8,  10, 10, 8,  9,  8,  8,
    8,  10, 8,  8,  8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 10, 9,  7,  9,  9,  10, 10, 10, 10, 10, 9,  10, 10, 10, 10,
    10, 10, 9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 9,  9,
    9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 9,  8,  9,  10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 9,  10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  9,  10, 10, 10,
    10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 9,  9,  9,  10, 10, 10, 10, 10, 10, 9,  9,  10, 9,  9,  8,  9,  8,  9,  4,
    6,  6,  6,  7,  8,  8,  9,  9,  10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    7,  10, 10, 10, 7,  10, 10, 7,  7,  7,  7,  7,  6,  7,  10, 7,  7,  10, 7,  7,  7,  6,  7,  6,  6,  7,  7,  6,  6,
    9,  6,  9,  10, 6,  10, 2,  6,  6,  7,  7,  8,  7,  8,  7,  8,  8,  9,  8,  9,  9,  9,  8,  8,  9,  9,  9,  10, 10,
    9,  8,  10, 9,  10, 9,  10, 9,  9,  6,  9,  8,  9,  9,  10, 9,  9,  9,  10, 9,  9,  9,  9,  8,  8,  8,  8,  8,  9,
    9,  9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 9,  7,  7,  8,  8,  8,  8,  9,  9,  7,  8,  9,  10, 8,  8,  7,  8,  8,
    10, 8,  8,  8,  9,  8,  9,  9,  10, 9,  11, 10, 11, 9,  9,  8,  7,  9,  8,  8,  6,  8,  8,  8,  7,  10, 9,  7,  8,
    7,  7,  8,  10, 7,  7,  7,  8,  9,  9,  9,  9,  10, 11, 9,  11, 10, 9,  7,  9,  10, 10, 10, 11, 11, 10, 10, 11, 10,
    10, 10, 11, 11, 10, 9,  10, 10, 11, 10, 11, 10, 11, 10, 10, 10, 11, 10, 11, 10, 10, 9,  10, 10, 11, 10, 10, 10, 10,
    9,  10, 10, 10, 10, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 10, 11, 10, 11, 10, 11, 11, 10, 8,  10, 10, 11, 10,
    11, 11, 11, 10, 11, 10, 11, 10, 11, 11, 11, 9,  10, 11, 11, 10, 11, 11, 11, 10, 11, 11, 11, 10, 10, 10, 10, 10, 11,
    10, 10, 11, 11, 10, 10, 9,  11, 10, 10, 11, 11, 10, 10, 10, 11, 10, 10, 10, 10, 10, 10, 9,  11, 10, 10, 8,  10, 8,
    6,  5,  6,  6,  7,  7,  8,  8,  8,  9,  10, 11, 10, 10, 11, 11, 12, 12, 10, 11, 12, 12, 12, 12, 13, 13, 13, 13, 13,
    12, 13, 13, 15, 14, 12, 14, 15, 16, 12, 12, 13, 15, 14, 16, 15, 17, 18, 15, 17, 16, 15, 15, 15, 15, 13, 13, 10, 14,
    12, 13, 17, 17, 18, 10, 17, 4,  7,  9,  9,  9,  9,  9,  9,  9,  9,  8,  9,  9,  9,  7,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  10, 9,  10, 9,  10, 9,  10, 9,  9,  5,  9,  7,  9,  9,  9,  9,  9,  7,  7,  7,  9,  7,  7,  8,  7,  8,  8,
    7,  7,  9,  9,  9,  9,  7,  7,  7,  9,  9,  9,  9,  9,  9,  7,  9,  7,  7,  7,  7,  9,  9,  7,  9,  9,  7,  7,  7,
    7,  7,  9,  7,  8,  7,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  7,  8,  7,  7,  7,  8,  8,  6,  7,  9,  7,
    7,  8,  7,  5,  6,  9,  5,  7,  5,  6,  7,  7,  9,  8,  9,  9,  9,  9,  9,  9,  9,  9,  10, 9,  10, 10, 10, 9,  9,
    10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 9,  10, 10, 10, 9,  9,
    10, 9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10,
    10, 9,  10, 10, 10, 9,  9,  9,  10, 10, 10, 10, 10, 9,  10, 9,  10, 10, 9,  10, 10, 9,  10, 10, 10, 10, 10, 10, 10,
    9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9,  10, 10, 10, 10, 10, 10, 10, 9,  10, 9,  10, 9,
    10, 10, 9,  5,  6,  8,  8,  7,  7,  7,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
    9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 9,  10, 10, 5,  10, 8,  9,  8,  9,
};

static const int8_t second_tree_lengths[5][321] = {
    4,  5,  6,  6,  7,  7,  6,  7,  7,  7,  6,  8,  7,  8,  8,  8,  8,  9,  6,  9,  8,  9,  8,  9,  9,  9,  8,  10, 5,
    9,  7,  9,  6,  9,  8,  10, 9,  10, 8,  8,  9,  9,  7,  9,  8,  9,  8,  9,  8,  8,  6,  9,  9,  8,  8,  9,  9,  10,
    8,  9,  9,  10, 8,  10, 8,  8,  8,  8,  8,  9,  7,  10, 6,  9,  9,  11, 7,  8,  8,  9,  8,  10, 7,  8,  6,  9,  10,
    9,  9,  10, 8,  11, 9,  11, 9,  10, 9,  8,  9,  8,  8,  8,  8,  10, 9,  9,  10, 10, 8,  9,  8,  8,  8,  11, 9,  8,
    8,  9,  9,  10, 8,  11, 10, 10, 8,  10, 9,  10, 8,  9,  9,  11, 9,  11, 9,  10, 10, 11, 10, 12, 9,  12, 10, 11, 10,
    11, 9,  10, 10, 11, 10, 11, 10, 11, 10, 11, 10, 10, 10, 9,  9,  9,  8,  7,  6,  8,  11, 11, 9,  12, 10, 12, 9,  11,
    11, 11, 10, 12, 11, 11, 10, 12, 10, 11, 10, 10, 10, 11, 10, 11, 11, 11, 9,  12, 10, 12, 11, 12, 10, 11, 10, 12, 11,
    12, 11, 12, 11, 12, 10, 12, 11, 12, 11, 11, 10, 12, 10, 11, 10, 12, 10, 12, 10, 12, 10, 11, 11, 11, 10, 11, 11, 11,
    10, 12, 11, 12, 10, 10, 11, 11, 9,  12, 11, 12, 10, 11, 10, 12, 10, 11, 10, 12, 10, 11, 10, 7,  5,  4,  6,  6,  7,
    7,  7,  8,  8,  7,  7,  6,  8,  6,  7,  7,  9,  8,  9,  9,  10, 11, 11, 11, 12, 11, 10, 11, 12, 11, 12, 11, 12, 12,
    12, 12, 11, 12, 12, 11, 12, 11, 12, 11, 13, 11, 12, 10, 13, 10, 14, 14, 13, 14, 15, 14, 16, 15, 15, 18, 18, 18, 9,
    18, 8,  5,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  8,  7,  8,  7,  7,  7,  8,  8,  8,  8,  9,  8,  9,  8,  9,  9,
    9,  7,  9,  8,  8,  6,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  9,  8,  8,  8,  8,  8,  9,  8,  9,  8,  9,
    9,  10, 8,  10, 8,  9,  9,  8,  8,  8,  7,  8,  8,  9,  8,  9,  7,  9,  8,  10, 8,  9,  8,  9,  8,  9,  8,  8,  8,
    9,  9,  9,  9,  10, 9,  11, 9,  10, 9,  10, 8,  8,  8,  9,  8,  8,  8,  9,  9,  8,  9,  10, 8,  9,  8,  8,  8,  11,
    8,  7,  8,  9,  9,  9,  9,  10, 9,  10, 9,  10, 9,  8,  8,  9,  9,  10, 9,  10, 9,  10, 8,  10, 9,  10, 9,  11, 10,
    11, 9,  11, 10, 10, 10, 11, 9,  11, 9,  10, 9,  11, 9,  11, 10, 10, 9,  10, 9,  9,  8,  10, 9,  11, 9,  9,  9,  11,
    10, 11, 9,  11, 9,  11, 9,  11, 10, 11, 10, 11, 10, 11, 9,  10, 10, 11, 10, 10, 8,  10, 9,  10, 10, 11, 9,  11, 9,
    10, 10, 11, 9,  10, 10, 9,  9,  10, 9,  10, 9,  10, 9,  10, 9,  11, 9,  11, 10, 10, 9,  10, 9,  11, 9,  11, 9,  11,
    9,  10, 9,  11, 9,  11, 9,  11, 9,  10, 8,  11, 9,  10, 9,  10, 9,  10, 8,  10, 8,  9,  8,  9,  8,  7,  4,  4,  5,
    6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  7,  8,  8,  9,  9,  10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 12, 11, 11, 12,
    12, 11, 12, 12, 11, 12, 12, 12, 12, 12, 12, 11, 12, 11, 13, 12, 13, 12, 13, 14, 14, 14, 15, 13, 14, 13, 14, 18, 18,
    17, 7,  16, 9,  5,  6,  6,  6,  6,  7,  7,  7,  6,  8,  7,  8,  7,  9,  8,  8,  7,  7,  8,  9,  9,  9,  9,  10, 8,
    9,  9,  10, 8,  10, 9,  8,  6,  10, 8,  10, 8,  10, 9,  9,  9,  9,  9,  10, 9,  9,  8,  9,  8,  9,  8,  9,  9,  10,
    9,  10, 9,  9,  8,  10, 9,  11, 10, 8,  8,  8,  8,  9,  7,  9,  9,  10, 8,  9,  8,  11, 9,  10, 9,  10, 8,  9,  9,
    9,  9,  8,  9,  9,  10, 10, 10, 12, 10, 11, 10, 10, 8,  9,  9,  9,  8,  9,  8,  8,  10, 9,  10, 11, 8,  10, 9,  9,
    8,  12, 8,  9,  9,  9,  9,  8,  9,  10, 9,  12, 10, 10, 10, 8,  7,  11, 10, 9,  10, 11, 9,  11, 7,  11, 10, 12, 10,
    12, 10, 11, 9,  11, 9,  12, 10, 12, 10, 12, 10, 9,  11, 12, 10, 12, 10, 11, 9,  10, 9,  10, 9,  11, 11, 12, 9,  10,
    8,  12, 11, 12, 9,  12, 10, 12, 10, 13, 10, 12, 10, 12, 10, 12, 10, 9,  10, 12, 10, 9,  8,  11, 10, 12, 10, 12, 10,
    12, 10, 11, 10, 12, 8,  12, 10, 11, 10, 10, 10, 12, 9,  11, 10, 12, 10, 12, 11, 12, 10, 9,  10, 12, 9,  10, 10, 12,
    10, 11, 10, 11, 10, 12, 8,  12, 9,  12, 8,  12, 8,  11, 10, 11, 10, 11, 9,  10, 8,  10, 9,  9,  8,  9,  8,  7,  4,
    3,  5,  5,  6,  5,  6,  6,  7,  7,  8,  8,  8,  7,  7,  7,  9,  8,  9,  9,  11, 9,  11, 9,  8,  9,  9,  11, 12, 11,
    12, 12, 13, 13, 12, 13, 14, 13, 14, 13, 14, 13, 13, 13, 12, 13, 13, 12, 13, 13, 14, 14, 13, 13, 14, 14, 14, 14, 15,
    18, 17, 18, 8,  16, 10, 4,  5,  6,  6,  6,  6,  7,  7,  6,  7,  7,  9,  6,  8,  8,  7,  7,  8,  8,  8,  6,  9,  8,
    8,  7,  9,  8,  9,  8,  9,  8,  9,  6,  9,  8,  9,  8,  10, 9,  9,  8,  10, 8,  10, 8,  9,  8,  9,  8,  8,  7,  9,
    9,  9,  9,  9,  8,  10, 9,  10, 9,  10, 9,  8,  7,  8,  9,  9,  8,  9,  9,  9,  7,  10, 9,  10, 9,  9,  8,  9,  8,
    9,  8,  8,  8,  9,  9,  10, 9,  9,  8,  11, 9,  11, 10, 10, 8,  8,  10, 8,  8,  9,  9,  9,  10, 9,  10, 11, 9,  9,
    9,  9,  8,  9,  8,  8,  8,  10, 10, 9,  9,  8,  10, 11, 10, 11, 11, 9,  8,  9,  10, 11, 9,  10, 11, 11, 9,  12, 10,
    10, 10, 12, 11, 11, 9,  11, 11, 12, 9,  11, 9,  10, 10, 10, 10, 12, 9,  11, 10, 11, 9,  11, 11, 11, 10, 11, 11, 12,
    9,  10, 10, 12, 11, 11, 10, 11, 9,  11, 10, 11, 10, 11, 9,  11, 11, 9,  8,  11, 10, 11, 11, 10, 7,  12, 11, 11, 11,
    11, 11, 12, 10, 12, 11, 13, 11, 10, 12, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 11, 11, 10, 11, 10, 10, 10, 11,
    10, 12, 11, 12, 10, 11, 9,  11, 10, 11, 10, 11, 10, 12, 9,  11, 11, 11, 9,  11, 10, 10, 9,  11, 10, 10, 9,  10, 9,
    7,  4,  5,  5,  5,  6,  6,  7,  6,  8,  7,  8,  9,  9,  7,  8,  8,  10, 9,  10, 10, 12, 10, 11, 11, 11, 11, 10, 11,
    12, 11, 11, 11, 11, 11, 13, 12, 11, 12, 13, 12, 12, 12, 13, 11, 9,  12, 13, 7,  13, 11, 13, 11, 10, 11, 13, 15, 15,
    12, 14, 15, 15, 15, 6,  15, 5,  8,  10, 11, 11, 11, 12, 11, 11, 12, 6,  11, 12, 10, 5,  12, 12, 12, 12, 12, 12, 12,
    13, 13, 14, 13, 13, 12, 13, 12, 13, 12, 15, 4,  10, 7,  9,  11, 11, 10, 9,  6,  7,  8,  9,  6,  7,  6,  7,  8,  7,
    7,  8,  8,  8,  8,  8,  8,  9,  8,  7,  10, 9,  10, 10, 11, 7,  8,  6,  7,  8,  8,  9,  8,  7,  10, 10, 8,  7,  8,
    8,  7,  10, 7,  6,  7,  9,  9,  8,  11, 11, 11, 10, 11, 11, 11, 8,  11, 6,  7,  6,  6,  6,  6,  8,  7,  6,  10, 9,
    6,  7,  6,  6,  7,  10, 6,  5,  6,  7,  7,  7,  10, 8,  11, 9,  13, 7,  14, 16, 12, 14, 14, 15, 15, 16, 16, 14, 15,
    15, 15, 15, 15, 15, 15, 15, 14, 15, 13, 14, 14, 16, 15, 17, 14, 17, 15, 17, 12, 14, 13, 16, 12, 17, 13, 17, 14, 13,
    13, 14, 14, 12, 13, 15, 15, 14, 15, 17, 14, 17, 15, 14, 15, 16, 12, 16, 15, 14, 15, 16, 15, 16, 17, 17, 15, 15, 17,
    17, 13, 14, 15, 15, 13, 12, 16, 16, 17, 14, 15, 16, 15, 15, 13, 13, 15, 13, 16, 17, 15, 17, 17, 17, 16, 17, 14, 17,
    14, 16, 15, 17, 15, 15, 14, 17, 15, 17, 15, 16, 15, 15, 16, 16, 14, 17, 17, 15, 15, 16, 15, 17, 15, 14, 16, 16, 16,
    16, 16, 12, 4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9,  9,  10, 10, 10, 11, 10, 11, 11,
    11, 11, 11, 12, 12, 12, 13, 13, 12, 13, 12, 14, 14, 12, 13, 13, 13, 13, 14, 12, 13, 13, 14, 14, 14, 13, 14, 14, 15,
    15, 13, 15, 13, 17, 17, 17, 9,  17, 7};

static const int8_t offset_tree_lengths[5][14] = {{5, 6, 3, 3, 3, 3, 3, 3, 3, 4, 6},
                                                  {5, 6, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 6},
                                                  {6, 7, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 5, 7},
                                                  {3, 6, 5, 4, 2, 3, 3, 3, 4, 4, 6},
                                                  {6, 7, 7, 6, 4, 3, 2, 2, 3, 3, 6}};

// This structure will represent a binary, prefix-free tree. The tree will in
// general be incomplete and unbalanced, and only a leaf nodes will hold a value.
// The path of s leaf node (from the root to the leaf node) will typically
// represent a binary code word corresponding to the value of the leaf node.

struct tree_node {
    struct tree_node *child[2];
    int value;
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Allocate and initialize a new tree node
struct tree_node *new_tree_node(void) {
    struct tree_node *p = malloc(sizeof(struct tree_node));
    if (!p)
        return NULL;

    p->child[0] = p->child[1] = NULL;
    p->value = INVALID_VALUE;

    return p;
}

// Free a tree and all of its nodes. Depth is bounded by MAX_CODE_LENGTH.
static void free_tree(struct tree_node *tree) {
    if (!tree)
        return;

    free_tree(tree->child[0]);
    free_tree(tree->child[1]);
    free(tree);
}

// Bit-level input buffer for reading variable-length codes from a byte buffer.
struct input_buffer {
    const uint8_t *buf;
    size_t size;
    uint32_t bit_offset;
    bool exhausted; // set when a read starts at or beyond the end of the buffer
};

// Read the next `i` bits from the input buffer and advance the bit offset.
// This reads a 32-bit word at the current byte offset and returns the
// requested bits (MSB within the selected window is the most significant
// remaining bit). Bytes beyond the end of the buffer read as zero; a read
// that starts at or beyond the end also sets `exhausted`.
static uint32_t next_bits(struct input_buffer *buf, int i) {
    if (i == 0)
        return 0;

    assert(i > 0 && i < 25);

    uint32_t byte_offset = buf->bit_offset >> 3;

    // Read a 32-bit word so we can shift out an arbitrary number of bits
    // without checking alignment on each call. Never load past the end of
    // the buffer: the last bytes of a fork are followed by unrelated data
    // or by the end of the archive allocation.
    uint32_t word = 0;
    size_t avail = byte_offset < buf->size ? buf->size - byte_offset : 0;
    if (avail == 0)
        buf->exhausted = true;
    else
        memcpy(&word, buf->buf + byte_offset, avail < sizeof(word) ? avail : sizeof(word));

    // Shift out already-consumed bits within the loaded word
    word >>= (buf->bit_offset & 7);

    // Mask to the requested width
    word &= (1u << i) - 1u;

    buf->bit_offset += (uint32_t)i;

    return word;
}

// Walk the prefix tree using bits from `buf` until a leaf value is found.
// Returns -1 if the bits do not form a code present in the tree.
int tree_to_value(struct tree_node *tree, struct input_buffer *buf) {
    while (tree->value == INVALID_VALUE) {
        tree = tree->child[next_bits(buf, 1)];

        // Trees built from stream data may be incomplete
        if (!tree)
            return -1;
    }

    return tree->value;
}

// Insert a code (bit sequence) of length `code_length` mapping to `value` into the tree.
// 'code' is interpreted MSB-first across the specified length. Returns 0 on
// success or -1 if a prefix collision is encountered or allocation fails.
static int add_code(struct tree_node *tree, uint32_t code, int code_length, int value) {
    // Traverse the tree following bits from most significant to least.
    while (code_length--) {

        // from most significant to least significant bit
        int next_bit = code >> code_length & 1;

        // If this node already holds a value, the new code would be a
        // descendant of an existing code (prefix collision).
        if (tree->value != INVALID_VALUE)
            return -1;

        if (!tree->child[next_bit]) {
            tree->child[next_bit] = new_tree_node();
            if (!tree->child[next_bit])
                return -1;
        }

        tree = tree->child[next_bit];
    }

    // Ensure a leaf node (no children, no value yet) before storing the value.
    if (tree->child[0] != NULL || tree->child[1] != NULL || tree->value != INVALID_VALUE)
        return -1;

    tree->value = value;
    return 0;
}

// Build a prefix tree from an array of code lengths per symbol. Uses a
// canonical assignment: codes of the same length are assigned sequentially.
// Returns NULL if the lengths do not describe a prefix-free code.
static struct tree_node *tree_from_code_lengths(const int8_t *code_lengths, int n_codes) {
    // The loop below only terminates once it has visited every symbol's
    // length, counting up from -1, so lengths outside this range must be
    // rejected up front.
    for (int i = 0; i < n_codes; i++)
        if (code_lengths[i] < -1 || code_lengths[i] > MAX_CODE_LENGTH)
            return NULL;

    struct tree_node *tree = new_tree_node();
    if (!tree)
        return NULL;

    // 'symbol' holds the next canonical code for the current length.
    uint32_t symbol = 0;
    for (int completed_symbols = 0, length = -1; completed_symbols < n_codes; length++, symbol <<= 1)

        for (int i = 0; i < n_codes; i++)
            if (code_lengths[i] == length) {
                // assign canonical code -> symbol
                if (length > 0 && add_code(tree, symbol, length, i) < 0) {
                    free_tree(tree);
                    return NULL;
                }

                symbol++;
                completed_symbols++;
            }

    return tree;
}

// Extract a tree by reading code-length metadata from the input using `metacode`.
// The metacode emits commands that modify or repeat the current code length.
// Returns NULL if the input is truncated or does not describe a valid tree.
struct tree_node *extract_tree_with_metacode(struct tree_node *metacode, struct input_buffer *input, int n_codes) {
    int8_t lengths[n_codes];

    // A repeat command names a store count; reject any that would run past the
    // end of the table rather than silently dropping the extra entries. A valid
    // stream stores exactly n_codes lengths, so this never fires for valid input.
    for (int length = 0, i = 0; i < n_codes;) {
        int next_code = tree_to_value(metacode, input);

        if (next_code < 0 || input->exhausted)
            return NULL;

        int repeats = 0; // extra stores this command makes before the trailing one
        if (next_code < 31)
            length = next_code + 1; // small values set new length = code+1
        else
            switch (next_code) {
            case 31:
                length = 0; // reset current length
                break;
            case 32:
                length++; // increment length
                break;
            case 33:
                length--; // decrement length
                break;
            case 34:
                // optional repeat of the current length (1 bit flag)
                repeats = next_bits(input, 1) ? 1 : 0;
                break;
            case 35:
                // repeat current length several times (3-bit count + 2)
                repeats = (int)next_bits(input, 3) + 2;
                break;
            case 36:
                // repeat current length many times (6-bit count + 10)
                repeats = (int)next_bits(input, 6) + 10;
                break;
            }

        // The repeats plus the one trailing store must all fit the table
        if (repeats > n_codes - i - 1)
            return NULL;
        while (repeats--)
            lengths[i++] = (int8_t)length;

        // Every command is followed by one store of the current length
        lengths[i++] = (int8_t)length;
    }

    return tree_from_code_lengths(lengths, n_codes);
}

// Internal streaming state wrapping the original decoder logic
typedef struct {
    struct input_buffer input;
    uint8_t window[UINT16_MAX + 1];
    int out_pos; // number of bytes produced so far (modulo isn't required for window)
    struct tree_node *first_tree;
    struct tree_node *second_tree;
    struct tree_node *offset_tree;
    struct tree_node *current_tree;
    int pending_match_len; // if >0, continue copying from match
    int pending_match_src; // source index for pending match
    bool initialized;
} sit13_state_t;

// Release the code trees owned by the state (the second may alias the first)
static void sit13_free_trees(sit13_state_t *st) {
    if (st->second_tree != st->first_tree)
        free_tree(st->second_tree);
    free_tree(st->first_tree);
    free_tree(st->offset_tree);
    st->first_tree = st->second_tree = st->offset_tree = st->current_tree = NULL;
}

// Shared setup code to build trees and initialize state
static int sit13_setup(sit13_state_t *st) {
    memset(st->window, 0, sizeof(st->window));
    st->out_pos = 0;
    st->pending_match_len = 0;
    st->pending_match_src = 0;

    int byte0 = (int)next_bits(&st->input, 8);
    int code_set = byte0 >> 4;
    if (code_set == 0) {
#define METACODE_SIZE 37
        const uint16_t meta_code_words[METACODE_SIZE] = {
            0x00dd, 0x001a, 0x0002, 0x0003, 0x0000, 0x000f, 0x0035, 0x0005, 0x0006, 0x0007, 0x001b, 0x0034, 0x0001,
            0x0001, 0x000e, 0x000c, 0x0036, 0x01bd, 0x0006, 0x000b, 0x000e, 0x001f, 0x001e, 0x0009, 0x0008, 0x000a,
            0x01bc, 0x01bf, 0x01be, 0x01b9, 0x01b8, 0x0004, 0x0002, 0x0001, 0x0007, 0x000c, 0x0002};
        const int meta_code_lengths[METACODE_SIZE] = {0xB, 0x8, 0x8, 0x8, 0x8, 0x7, 0x6, 0x5, 0x5, 0x5, 0x5, 0x6, 0x5,
                                                      0x6, 0x7, 0x7, 0x9, 0xC, 0xA, 0xB, 0xB, 0xC, 0xC, 0xB, 0xB, 0xB,
                                                      0xC, 0xC, 0xC, 0xC, 0xC, 0x5, 0x2, 0x2, 0x3, 0x4, 0x5};
        struct tree_node *meta_code = new_tree_node();
        bool meta_ok = (meta_code != NULL);
        for (int i = 0; meta_ok && i < METACODE_SIZE; i++)
            meta_ok = (add_code(meta_code, meta_code_words[i], meta_code_lengths[i], i) == 0);

        // Stop at the first tree that cannot be built; the check below reports it
        if (meta_ok)
            st->first_tree = extract_tree_with_metacode(meta_code, &st->input, MAX_CODE);
        if (st->first_tree)
            st->second_tree =
                (byte0 & 0x08) ? st->first_tree : extract_tree_with_metacode(meta_code, &st->input, MAX_CODE);
        if (st->second_tree)
            st->offset_tree = extract_tree_with_metacode(meta_code, &st->input, (byte0 & 0x07) + 10);

        // The metacode tree is only needed while reading the code lengths
        free_tree(meta_code);
    } else if (code_set < 6) {
        const int offset_tree_length[5] = {11, 13, 14, 11, 11};
        st->first_tree = tree_from_code_lengths(first_tree_lengths[code_set - 1], MAX_CODE);
        st->second_tree = tree_from_code_lengths(second_tree_lengths[code_set - 1], MAX_CODE);
        st->offset_tree = tree_from_code_lengths(offset_tree_lengths[code_set - 1], offset_tree_length[code_set - 1]);
    } else {
        return MUNBOX_ERROR;
    }

    // Code lengths taken from the stream may not describe usable trees
    if (!st->first_tree || !st->second_tree || !st->offset_tree) {
        sit13_free_trees(st);
        return MUNBOX_ERROR;
    }
    st->current_tree = st->first_tree;
    st->initialized = true;
    return 0;
}

// Produce up to out_cap bytes into out, reading from st->input
static ssize_t sit13_produce(sit13_state_t *st, uint8_t *out, size_t out_cap) {
    size_t produced = 0;
    while (produced < out_cap) {
        // If we have a pending match copy, service it
        if (st->pending_match_len > 0) {
            uint8_t b = st->window[st->pending_match_src++ & UINT16_MAX];
            out[produced++] = b;
            st->window[st->out_pos & UINT16_MAX] = b;
            st->out_pos++;
            if (--st->pending_match_len == 0) {
                st->current_tree = st->second_tree; // after a match, switch to second tree
            }
            continue;
        }

        // Decode next symbol
        int next_value;
        if (st->current_tree->value == INVALID_VALUE) {
            next_value = tree_to_value(st->current_tree, &st->input);
        } else {
            next_value = st->current_tree->value;
        }

        // Undecodable symbol, or the compressed data ended early
        if (next_value < 0 || st->input.exhausted)
            return MUNBOX_ERROR;

        if (next_value < 256) {
            uint8_t b = (uint8_t)next_value;
            out[produced++] = b;
            st->window[st->out_pos & UINT16_MAX] = b;
            st->out_pos++;
            st->current_tree = st->first_tree;
            continue;
        }

        int length = 0;
        if (next_value < 318) {
            length = next_value - 253;
        } else if (next_value == 318) {
            length = (int)next_bits(&st->input, 10) + 65;
        } else if (next_value == 319) {
            length = (int)next_bits(&st->input, 15) + 65;
        } else {
            return MUNBOX_ERROR;
        }
        // Offset via offset_tree
        int ov = tree_to_value(st->offset_tree, &st->input);
        if (ov < 0)
            return MUNBOX_ERROR;
        int offset = (ov == 0) ? 1 : ((1 << (ov - 1)) + (int)next_bits(&st->input, ov - 1) + 1);
        int src_index = st->out_pos - offset;

        // If the requested match is longer than remaining capacity, stage it
        st->pending_match_len = length;
        st->pending_match_src = src_index;
        // Loop will continue and copy as much as fits this call
    }
    return (ssize_t)produced;
}

// One-shot wrapper retained for compatibility/tests
size_t sit13(uint8_t *dst, size_t dst_len, const uint8_t *src, size_t src_len) {
    sit13_state_t st = {0};
    st.input.buf = src;
    st.input.size = src_len;
    st.input.bit_offset = 0;
    if (sit13_setup(&st) < 0)
        return 0;
    ssize_t n = sit13_produce(&st, dst, dst_len);
    sit13_free_trees(&st);
    return (n < 0) ? 0 : (size_t)n;
}

// Public streaming API
struct sit13_ctx {
    sit13_state_t st;
};

sit13_ctx_t *sit13_init(const uint8_t *src, size_t src_len) {
    if (!src && src_len)
        return NULL;
    sit13_ctx_t *ctx = (sit13_ctx_t *)calloc(1, sizeof(sit13_ctx_t));
    if (!ctx)
        return NULL;
    ctx->st.input.buf = src;
    ctx->st.input.size = src_len;
    ctx->st.input.bit_offset = 0;
    if (sit13_setup(&ctx->st) < 0) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

ssize_t sit13_read(sit13_ctx_t *ctx, uint8_t *out, size_t out_cap) {
    if (!ctx || !out)
        return MUNBOX_ERROR;
    return sit13_produce(&ctx->st, out, out_cap);
}

void sit13_free(sit13_ctx_t *ctx) {
    if (!ctx)
        return;
    sit13_free_trees(&ctx->st);
    free(ctx);
}
