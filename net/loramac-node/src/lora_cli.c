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

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(LORAMAC_NODE_CLI)

#include <inttypes.h>
#include <string.h>

#include "sysinit/sysinit.h"
#include "shell/shell.h"
#include "console/console.h"
#include "loramac-node/radio.h"
#include "parse/parse.h"

static int lora_cli_cmd_fn(int argc, char **argv);
static int lora_cli_set_freq(int argc, char **argv);
static int lora_cli_tx_cfg(int argc, char **argv);
static int lora_cli_rx_cfg(int argc, char **argv);
static int lora_cli_tx(int argc, char **argv);
static int lora_cli_rx(int argc, char **argv);

static struct shell_cmd lora_cli_cmd = {
    .sc_cmd = "lora",
    .sc_cmd_func = lora_cli_cmd_fn,
};

static struct shell_cmd lora_cli_subcmds[] = {
    {
        .sc_cmd = "set_freq",
        .sc_cmd_func = lora_cli_set_freq,
    },
    {
        .sc_cmd = "tx_cfg",
        .sc_cmd_func = lora_cli_tx_cfg,
    },
    {
        .sc_cmd = "rx_cfg",
        .sc_cmd_func = lora_cli_rx_cfg,
    },
    {
        .sc_cmd = "tx",
        .sc_cmd_func = lora_cli_tx,
    },
    {
        .sc_cmd = "rx",
        .sc_cmd_func = lora_cli_rx,
    },
};

static int
lora_cli_cmd_fn(int argc, char **argv)
{
    const struct shell_cmd *subcmd;
    int rc;
    int i;

    if (argc <= 1) {
        /* XXX: Print usage. */
        return 1;
    }

    for (i = 0;
         i < sizeof lora_cli_subcmds / sizeof lora_cli_subcmds[0];
         i++) {

        subcmd = lora_cli_subcmds + i;
        if (strcmp(argv[1], subcmd->sc_cmd) == 0) {
            rc = subcmd->sc_cmd_func(argc - 1, argv + 1);
            if (rc != 0) {
                console_printf("Erroneous request\n");
            }
            return rc;
        }
    }

    console_printf("Invalid lora command: %s\n", argv[1]);
    /* XXX: Print usage. */
    return 1;
}

static int
lora_cli_set_freq(int argc, char **argv)
{
    uint32_t freq;
    int rc;

    if (argc <= 1) {
        /* XXX: Print usage. */
        return 1;
    }

    freq = parse_ull(argv[1], &rc);
    if (rc != 0) {
        return rc;
    }

    Radio.SetChannel(freq);
    return 0;
}

static int
lora_cli_tx_cfg(int argc, char **argv)
{
    RadioModems_t modem;
    char **arg;
    uint32_t bandwidth;
    uint32_t datarate;
    uint32_t timeout;
    uint32_t fdev;
    uint16_t preamble_len;
    uint8_t hop_period;
    uint8_t coderate;
    int8_t power;
    int freq_hop_on;
    int iq_inverted;
    int fix_len;
    int crc_on;
    int rc;

    if (argc <= 13) {
        /* XXX: Print usage. */
        return 1;
    }

    arg = argv + 1;

    modem = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    power = parse_ll_bounds(*arg, INT8_MIN, INT8_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    fdev = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    bandwidth = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    datarate = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    coderate = parse_ull_bounds(*arg, 0, UINT8_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    preamble_len = parse_ull_bounds(*arg, 0, UINT16_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    fix_len = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    crc_on = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    freq_hop_on = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    hop_period = parse_ull_bounds(*arg, 0, UINT8_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    iq_inverted = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    timeout = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    Radio.SetTxConfig(modem,
                      power,
                      fdev,
                      bandwidth,
                      datarate,
                      coderate,
                      preamble_len,
                      fix_len,
                      crc_on,
                      freq_hop_on,
                      hop_period,
                      iq_inverted,
                      timeout);

    return 0;
}

static int
lora_cli_rx_cfg(int argc, char **argv)
{
    RadioModems_t modem;
    char **arg;
    uint32_t bandwidth_afc;
    uint32_t bandwidth;
    uint32_t datarate;
    uint16_t preamble_len;
    uint16_t symb_timeout;
    uint8_t payload_len;
    uint8_t hop_period;
    uint8_t coderate;
    int rx_continuous;
    int freq_hop_on;
    int iq_inverted;
    int fix_len;
    int crc_on;
    int rc;

    if (argc <= 14) {
        /* XXX: Print usage. */
        return 1;
    }

    arg = argv + 1;

    modem = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    bandwidth = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    datarate = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    coderate = parse_ull_bounds(*arg, 0, UINT8_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    bandwidth_afc = parse_ull_bounds(*arg, 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    preamble_len = parse_ull_bounds(*arg, 0, UINT16_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    symb_timeout = parse_ull_bounds(*arg, 0, UINT16_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    fix_len = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    payload_len = parse_ull_bounds(*arg, 0, UINT8_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    crc_on = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    freq_hop_on = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    hop_period = parse_ull_bounds(*arg, 0, UINT8_MAX, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    iq_inverted = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    rx_continuous = parse_ull_bounds(*arg, 0, 1, &rc);
    if (rc != 0) {
        return rc;
    }
    arg++;

    Radio.SetRxConfig(modem,
                      bandwidth,
                      datarate,
                      coderate,
                      bandwidth_afc,
                      preamble_len,
                      symb_timeout,
                      fix_len,
                      payload_len,
                      crc_on,
                      freq_hop_on,
                      hop_period,
                      iq_inverted,
                      rx_continuous);

    return 0;
}

static int
lora_cli_tx(int argc, char **argv)
{
    uint8_t buf[UINT8_MAX];
    int buf_sz;
    int rc;

    if (argc <= 1) {
        /* XXX: Print usage. */
        return 1;
    }

    rc = parse_byte_stream(argv[1], sizeof buf, buf, &buf_sz);
    if (rc != 0) {
        return rc;
    }

    Radio.Send(buf, buf_sz);
    return 0;
}

static int
lora_cli_rx(int argc, char **argv)
{
    uint32_t timeout;
    int rc;

    if (argc <= 1) {
        /* XXX: Print usage. */
        return 1;
    }

    timeout = parse_ull_bounds(argv[1], 0, UINT32_MAX, &rc);
    if (rc != 0) {
        return rc;
    }

    Radio.Rx(timeout);
    return 0;
}

void
lora_cli_init(void)
{
    int rc;

    rc = shell_cmd_register(&lora_cli_cmd);
    SYSINIT_PANIC_ASSERT_MSG(rc == 0, "Failed to register lora CLI command");
}

#endif /* MYNEWT_VAL(LORAMAC_NODE_CLI) */
