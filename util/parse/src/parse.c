/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include "defs/error.h"

static int
parse_num_base(const char *sval)
{
    /* Skip optional sign. */
    if (sval[0] == '+' || sval[0] == '-') {
        sval++;
    }

    if (sval[0] == '0' && sval[1] == 'x') {
        return 0;
    } else {
        return 10;
    }
}

long long
parse_ll_bounds(const char *sval, long long min, long long max,
                int *out_status)
{
    char *endptr;
    long long llval;

    llval = strtoll(sval, &endptr, parse_num_base(sval));
    if (sval[0] != '\0' && *endptr == '\0' &&
        llval >= min && llval <= max) {

        *out_status = 0;
        return llval;
    }

    *out_status = SYS_EINVAL;
    return 0;
}

unsigned long long
parse_ull_bounds(const char *sval,
                 unsigned long long min, unsigned long long max,
                 int *out_status)
{
    char *endptr;
    unsigned long long ullval;

    ullval = strtoull(sval, &endptr, parse_num_base(sval));
    if (sval[0] != '\0' && *endptr == '\0' &&
        ullval >= min && ullval <= max) {

        *out_status = 0;
        return ullval;
    }

    *out_status = SYS_EINVAL;
    return 0;
}

long long
parse_ll(const char *sval, int *out_status)
{
    return parse_ll_bounds(sval, LLONG_MIN, LLONG_MAX, out_status);
}

long long
parse_ull(const char *sval, int *out_status)
{
    return parse_ull_bounds(sval, 0, ULLONG_MAX, out_status);
}

int
parse_byte_stream_delim(const char *sval, const char *delims, int max_len,
                        uint8_t *dst, int *out_len)
{
    unsigned long ul;
    const char *cur;
    char *endptr;
    int i;

    i = 0;
    cur = sval;
    while (*cur != '\0') {
        if (i >= max_len) {
            return SYS_ERANGE;
        }

        ul = strtoul(cur, &endptr, parse_num_base(cur));
        if (endptr == cur) {
            return SYS_EINVAL;
        }
        cur = endptr;

        if (*cur != '\0') {
            if (strspn(cur, delims) != 1) {
                return SYS_EINVAL;
            }
            cur++;
            if (*cur == '\0') {
                /* Ended with a delimiter. */
                return SYS_EINVAL;
            }
        }

        if (ul > UINT8_MAX) {
            return SYS_EINVAL;
        }

        dst[i] = ul;
        i++;
    }

    *out_len = i;

    return 0;
}

int
parse_byte_stream(const char *sval, int max_len, uint8_t *dst, int *out_len)
{
    return parse_byte_stream_delim(sval, ":-", max_len, dst, out_len);
}

int
parse_byte_stream_exact_length(const char *sval, uint8_t *dst, int len)
{
    int actual_len;
    int rc;

    rc = parse_byte_stream(sval, len, dst, &actual_len);
    if (rc != 0) {
        return rc;
    }

    if (actual_len != len) {
        return SYS_EINVAL;
    }

    return 0;
}
