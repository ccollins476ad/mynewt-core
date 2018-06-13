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

#include "os/mynewt.h"

#if MYNEWT_VAL(LOG_CONSOLE)

#include <cbmem/cbmem.h>
#include <console/console.h>
#include "log/log.h"

static int
log_console_append_start(struct log *log, const struct log_entry_hdr *hdr,
                         int body_len)
{
    if (console_is_init() && !console_is_midline) {
        console_printf("[ts=%llussb, mod=%u level=%u] ",
                hdr->ue_ts, hdr->ue_module, log_entry_get_level(hdr));
    }

    return 0;
}

static int
log_console_append_chunk(struct log *log, const void *buf, int len)
{
    if (console_is_init()) {
        console_write(buf, len);
    }

    return (0);
}

static int
log_console_append_finish(struct log *log)
{
    return 0;
}

static int
log_console_read(struct log *log, void *dptr, void *buf, uint16_t offset,
        uint16_t len)
{
    /* You don't read console, console read you */
    return (OS_EINVAL);
}

static int
log_console_walk(struct log *log, log_walk_func_t walk_func,
        struct log_offset *log_offset)
{
    /* You don't walk console, console walk you. */
    return (OS_EINVAL);
}

static int
log_console_flush(struct log *log)
{
    /* You don't flush console, console flush you. */
    return (OS_EINVAL);
}

const struct log_handler log_console_handler = {
    .log_type = LOG_TYPE_STREAM,
    .log_read = log_console_read,
    .log_append_start = log_console_append_start,
    .log_append_chunk = log_console_append_chunk,
    .log_append_finish = log_console_append_finish,
    .log_walk = log_console_walk,
    .log_flush = log_console_flush,
};

#endif
