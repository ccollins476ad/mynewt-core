/**
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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_cputime.h"
#include "hal/hal_uart.h"

/* BLE */
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_transport.h"

#define HCI_UART_SPEED      1000000
#define HCI_UART_FLOW_CTL   HAL_UART_FLOW_CTL_RTS_CTS
#define HCI_UART            CONSOLE_UART

#define HCI_CMD_HDR_LEN 3
#define HCI_ACL_HDR_LEN 4
#define HCI_EVT_HDR_LEN 2

#define H4_NONE 0x00
#define H4_CMD  0x01
#define H4_ACL  0x02
#define H4_SCO  0x03
#define H4_EVT  0x04

#define BLE_HCI_TRANS_EVENT_CMD     (OS_EVENT_T_PERUSER + 0)
#define BLE_HCI_TRANS_EVENT_EVT     (OS_EVENT_T_PERUSER + 1)
#define BLE_HCI_TRANS_EVENT_ACL     (OS_EVENT_T_PERUSER + 2)

static ble_hci_trans_rx_cmd_fn *ble_hci_uart_rx_cmd_cb;
static void *ble_hci_uart_rx_cmd_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_uart_rx_acl_cb;
static void *ble_hci_uart_rx_acl_arg;

static struct os_mempool ble_hci_uart_evt_pool;
static void *ble_hci_uart_evt_buf;

static struct os_mempool ble_hci_uart_os_evt_pool;
static void *ble_hci_uart_os_evt_buf;

#define BLE_HCI_UART_LOG_SZ 1024
static uint8_t ble_hci_uart_tx_log[BLE_HCI_UART_LOG_SZ];
static int ble_hci_uart_tx_log_sz;
static uint8_t ble_hci_uart_rx_log[BLE_HCI_UART_LOG_SZ];
static int ble_hci_uart_rx_log_sz;

struct memblock {
    uint8_t *data;      /* Pointer to memblock data */
    uint16_t cur;       /* Number of bytes read/written */
    uint16_t len;       /* Total number of bytes to read/write */
};

struct ota_acl {
    struct os_mbuf *buf; /* Buffer containing the data */
    uint16_t len;        /* Target size when buf is considered complete */
};

static struct {
    /* State of data from host to controller */
    uint8_t ota_type;    /* Pending packet type. 0 means nothing pending */
    union {
        struct memblock ota_cmd;
        struct ota_acl ota_acl;
    };

    /* State of data from controller to host */
    uint8_t hci_type;    /* Pending packet type. 0 means nothing pending */
    union {
        struct memblock hci_cmd;
        struct os_mbuf *hci_acl;
    };
    STAILQ_HEAD(, os_event) hci_pkts; /* Packet queue to send to UART */
} hci;

static int
ble_hci_uart_acl_tx(struct os_mbuf *om)
{
    struct os_event *ev;
    os_sr_t sr;
    int rc;

    ev = os_memblock_get(&ble_hci_uart_os_evt_pool);
    if (ev != NULL) {
        ev->ev_type = BLE_HCI_TRANS_EVENT_ACL;
        ev->ev_arg = om;
        ev->ev_queued = 1;

        OS_ENTER_CRITICAL(sr);
        STAILQ_INSERT_TAIL(&hci.hci_pkts, ev, ev_next);
        OS_EXIT_CRITICAL(sr);

        hal_uart_start_tx(HCI_UART);

        rc = 0;
    } else {
        os_mbuf_free_chain(om);
        rc = -1;
    }

    return rc;
}

static int
ble_hci_uart_cmdevt_tx(uint8_t *hci_ev, uint8_t h4_type)
{
    struct os_event *ev;
    os_sr_t sr;
    int rc;

    ev = os_memblock_get(&ble_hci_uart_os_evt_pool);
    if (!ev) {
        rc = ble_hci_trans_free_buf(hci_ev);
        assert(rc == OS_OK);

        return -1;
    }

    ev->ev_type = h4_type;
    ev->ev_arg = hci_ev;
    ev->ev_queued = 1;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&hci.hci_pkts, ev, ev_next);
    OS_EXIT_CRITICAL(sr);

    hal_uart_start_tx(HCI_UART);

    return 0;
}

static int
ble_hci_uart_tx_pkt_type(void)
{
    struct os_event *ev;
    os_sr_t sr;
    int rc;

    OS_ENTER_CRITICAL(sr);

    ev = STAILQ_FIRST(&hci.hci_pkts);
    if (!ev) {
        OS_EXIT_CRITICAL(sr);
        return -1;
    }

    STAILQ_REMOVE(&hci.hci_pkts, ev, os_event, ev_next);
    ev->ev_queued = 0;

    OS_EXIT_CRITICAL(sr);

    switch (ev->ev_type) {
    case BLE_HCI_TRANS_EVENT_CMD:
        hci.hci_type = H4_CMD;
        hci.hci_cmd.data = ev->ev_arg;
        hci.hci_cmd.cur = 0;
        hci.hci_cmd.len = hci.hci_cmd.data[2] + HCI_CMD_HDR_LEN;
        rc = H4_CMD;
        break;

    case BLE_HCI_TRANS_EVENT_EVT:
        hci.hci_type = H4_EVT;
        hci.hci_cmd.data = ev->ev_arg;
        hci.hci_cmd.cur = 0;
        hci.hci_cmd.len = hci.hci_cmd.data[1] + HCI_EVT_HDR_LEN;
        rc = H4_EVT;
        break;

    case BLE_HCI_TRANS_EVENT_ACL:
        hci.hci_type = H4_ACL;
        hci.hci_acl = ev->ev_arg;
        rc = H4_ACL;
        break;
    default:
        rc = -1;
        break;
    }

    os_memblock_put(&ble_hci_uart_os_evt_pool, ev);

    return rc;
}

static int
ble_hci_uart_tx_char(void *arg)
{
    int rc = -1;

    switch (hci.hci_type) {
    case H4_NONE: /* No pending packet, pick one from the queue */
        rc = ble_hci_uart_tx_pkt_type();
        break;

    case H4_CMD:
    case H4_EVT:
        rc = hci.hci_cmd.data[hci.hci_cmd.cur++];

        if (hci.hci_cmd.cur == hci.hci_cmd.len) {
            ble_hci_trans_free_buf(hci.hci_cmd.data);
            hci.hci_type = H4_NONE;
        }
        break;

    case H4_ACL:
        rc = *OS_MBUF_DATA(hci.hci_acl, uint8_t *);
        os_mbuf_adj(hci.hci_acl, 1);
        if (!OS_MBUF_PKTLEN(hci.hci_acl)) {
            os_mbuf_free_chain(hci.hci_acl);
            hci.hci_type = H4_NONE;
        }
        break;
    }

    if (rc != -1) {
        ble_hci_uart_tx_log[ble_hci_uart_tx_log_sz++] = rc;
        if (ble_hci_uart_tx_log_sz == sizeof ble_hci_uart_tx_log) {
            ble_hci_uart_tx_log_sz = 0;
        }
    }

    return rc;
}

static int
ble_hci_uart_rx_pkt_type(uint8_t data)
{
    hci.ota_type = data;

    /* XXX: For now we assert that buffer allocation succeeds.  The correct
     * thing to do is return -1 on allocation failure so that flow control is
     * engaged.  Then, we will need to tell the UART to start receiving again
     * when we free a buffer.
     */
    switch (hci.ota_type) {
    case H4_CMD:
        hci.ota_cmd.data = ble_hci_trans_alloc_buf(BLE_HCI_TRANS_BUF_CMD);
        assert(hci.ota_cmd.data != NULL);

        hci.ota_cmd.len = 0;
        hci.ota_cmd.cur = 0;
        break;

    case H4_EVT:
        hci.ota_cmd.data = ble_hci_trans_alloc_buf(BLE_HCI_TRANS_BUF_EVT_HI);
        assert(hci.ota_cmd.data != NULL);

        hci.ota_cmd.len = 0;
        hci.ota_cmd.cur = 0;
        break;

    case H4_ACL:
        hci.ota_acl.buf = os_msys_get_pkthdr(HCI_ACL_HDR_LEN, 0);
        assert(hci.ota_acl.buf != NULL);

        hci.ota_acl.len = 0;
        break;

    default:
        hci.ota_type = H4_NONE;
        return -1;
    }

    return 0;
}

static int
ble_hci_uart_rx_cmd(uint8_t data)
{
    int rc;

    hci.ota_cmd.data[hci.ota_cmd.cur++] = data;

    if (hci.ota_cmd.cur < HCI_CMD_HDR_LEN) {
        return 0;
    } else if (hci.ota_cmd.cur == HCI_CMD_HDR_LEN) {
        hci.ota_cmd.len = hci.ota_cmd.data[2] + HCI_CMD_HDR_LEN;
    }

    if (hci.ota_cmd.cur == hci.ota_cmd.len) {
        assert(ble_hci_uart_rx_cmd_cb != NULL);
        rc = ble_hci_uart_rx_cmd_cb(hci.ota_cmd.data, ble_hci_uart_rx_cmd_arg);
        if (rc != 0) {
            rc = ble_hci_trans_free_buf(hci.ota_cmd.data);
            assert(rc == 0);
        }
        hci.ota_type = H4_NONE;
    }

    return 0;
}

static int
ble_hci_uart_rx_evt(uint8_t data)
{
    int rc;

    hci.ota_cmd.data[hci.ota_cmd.cur++] = data;

    if (hci.ota_cmd.cur < HCI_EVT_HDR_LEN) {
        return 0;
    } else if (hci.ota_cmd.cur == HCI_EVT_HDR_LEN) {
        hci.ota_cmd.len = hci.ota_cmd.data[1] + HCI_EVT_HDR_LEN;
    }

    if (hci.ota_cmd.cur == hci.ota_cmd.len) {
        assert(ble_hci_uart_rx_cmd_cb != NULL);
        rc = ble_hci_uart_rx_cmd_cb(hci.ota_cmd.data, ble_hci_uart_rx_cmd_arg);
        if (rc != 0) {
            rc = ble_hci_trans_free_buf(hci.ota_cmd.data);
            assert(rc == 0);
        }
        hci.ota_type = H4_NONE;
    }

    return 0;
}

static int
ble_hci_uart_rx_acl(uint8_t data)
{
    os_mbuf_append(hci.ota_acl.buf, &data, 1);

    if (OS_MBUF_PKTLEN(hci.ota_acl.buf) < HCI_ACL_HDR_LEN) {
        return 0;
    } else if (OS_MBUF_PKTLEN(hci.ota_acl.buf) == HCI_ACL_HDR_LEN) {
        os_mbuf_copydata(hci.ota_acl.buf, 2, sizeof(hci.ota_acl.len),
                         &hci.ota_acl.len);
        hci.ota_acl.len = le16toh(&hci.ota_acl.len) + HCI_ACL_HDR_LEN;
    }

    if (OS_MBUF_PKTLEN(hci.ota_acl.buf) == hci.ota_acl.len) {
        assert(ble_hci_uart_rx_cmd_cb != NULL);
        ble_hci_uart_rx_acl_cb(hci.ota_acl.buf, ble_hci_uart_rx_acl_arg);
        hci.ota_type = H4_NONE;
    }

    return 0;
}

static int
ble_hci_uart_rx_char(void *arg, uint8_t data)
{
    ble_hci_uart_rx_log[ble_hci_uart_rx_log_sz++] = data;
    if (ble_hci_uart_rx_log_sz == sizeof ble_hci_uart_rx_log) {
        ble_hci_uart_rx_log_sz = 0;
    }

    switch (hci.ota_type) {
    case H4_NONE:
        return ble_hci_uart_rx_pkt_type(data);
    case H4_CMD:
        return ble_hci_uart_rx_cmd(data);
    case H4_EVT:
        return ble_hci_uart_rx_evt(data);
    case H4_ACL:
        return ble_hci_uart_rx_acl(data);
    default:
        return -1;
    }
}

static void
ble_hci_uart_set_rx_cbs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                        void *cmd_arg,
                        ble_hci_trans_rx_acl_fn *acl_cb,
                        void *acl_arg)
{
    ble_hci_uart_rx_cmd_cb = cmd_cb;
    ble_hci_uart_rx_cmd_arg = cmd_arg;
    ble_hci_uart_rx_acl_cb = acl_cb;
    ble_hci_uart_rx_acl_arg = acl_arg;
}

static void
ble_hci_uart_free_mem(void)
{
    free(ble_hci_uart_evt_buf);
    ble_hci_uart_evt_buf = NULL;

    free(ble_hci_uart_os_evt_buf);
    ble_hci_uart_os_evt_buf = NULL;
}

int
ble_hci_trans_hs_acl_send(struct os_mbuf *om)
{
    int rc;

    rc = ble_hci_uart_acl_tx(om);
    return rc;
}

int
ble_hci_trans_ll_acl_send(struct os_mbuf *om)
{
    int rc;

    rc = ble_hci_uart_acl_tx(om);
    return rc;
}

int
ble_hci_trans_hs_cmd_send(uint8_t *cmd)
{
    int rc;

    rc = ble_hci_uart_cmdevt_tx(cmd, BLE_HCI_TRANS_EVENT_CMD);
    return rc;
}

int
ble_hci_trans_ll_evt_send(uint8_t *cmd)
{
    int rc;

    rc = ble_hci_uart_cmdevt_tx(cmd, BLE_HCI_TRANS_EVENT_EVT);
    return rc;
}

void
ble_hci_trans_set_rx_cbs_hs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                            void *cmd_arg,
                            ble_hci_trans_rx_acl_fn *acl_cb,
                            void *acl_arg)
{
    ble_hci_uart_set_rx_cbs(cmd_cb, cmd_arg, acl_cb, acl_arg);
}

void
ble_hci_trans_set_rx_cbs_ll(ble_hci_trans_rx_cmd_fn *cmd_cb,
                            void *cmd_arg,
                            ble_hci_trans_rx_acl_fn *acl_cb,
                            void *acl_arg)
{
    ble_hci_uart_set_rx_cbs(cmd_cb, cmd_arg, acl_cb, acl_arg);
}

uint8_t *
ble_hci_trans_alloc_buf(int type)
{
    uint8_t *buf;

    switch (type) {
    case BLE_HCI_TRANS_BUF_CMD:
    case BLE_HCI_TRANS_BUF_EVT_LO:
    case BLE_HCI_TRANS_BUF_EVT_HI:
        buf = os_memblock_get(&ble_hci_uart_evt_pool);
        break;

    default:
        assert(0);
        buf = NULL;
    }

    return buf;
}

int
ble_hci_trans_free_buf(uint8_t *buf)
{
    int rc;

    rc = os_memblock_put(&ble_hci_uart_evt_pool, buf);
    return rc;
}

int
ble_hci_uart_init(int num_evt_bufs, int buf_size)
{
    int rc;

    ble_hci_uart_free_mem();

    ble_hci_uart_evt_buf = malloc(OS_MEMPOOL_BYTES(num_evt_bufs, buf_size));
    if (ble_hci_uart_evt_buf == NULL) {
        rc = ENOMEM;
        goto err;
    }

    /* Create memory pool of command buffers */
    rc = os_mempool_init(&ble_hci_uart_evt_pool, num_evt_bufs,
                         buf_size, ble_hci_uart_evt_buf,
                         "ble_hci_uart_evt_pool");
    if (rc != 0) {
        return EINVAL;
    }

    ble_hci_uart_os_evt_buf = malloc(
        OS_MEMPOOL_BYTES(num_evt_bufs, sizeof (struct os_event)));
    if (ble_hci_uart_os_evt_buf == NULL) {
        rc = ENOMEM;
        goto err;
    }

    /* Create memory pool of command buffers */
    rc = os_mempool_init(&ble_hci_uart_os_evt_pool, num_evt_bufs,
                         sizeof (struct os_event), ble_hci_uart_os_evt_buf,
                         "ble_hci_uart_os_evt_pool");
    if (rc != 0) {
        return EINVAL;
    }

    memset(&hci, 0, sizeof(hci));

    STAILQ_INIT(&hci.hci_pkts);

    rc = hal_uart_init_cbs(HCI_UART, ble_hci_uart_tx_char, NULL,
                           ble_hci_uart_rx_char, NULL);
    if (rc != 0) {
        return rc;
    }

    rc = hal_uart_config(HCI_UART, HCI_UART_SPEED, 8, 1,
                         HAL_UART_PARITY_NONE, HCI_UART_FLOW_CTL);
    if (rc != 0) {
        return rc;
    }

    return 0;

err:
    ble_hci_uart_free_mem();
    return rc;
}
