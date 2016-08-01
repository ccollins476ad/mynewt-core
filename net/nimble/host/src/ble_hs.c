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
#include <errno.h>
#include "bsp/bsp.h"
#include "stats/stats.h"
#include "util/tpq.h"
#include "os/os.h"
#include "nimble/hci_transport.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"

/**
 * The maximum number of events the host will process in a row before returning
 * control to the parent task.
 */
#define BLE_HS_MAX_EVS_IN_A_ROW 2

static struct log_handler ble_hs_log_console_handler;

struct os_mempool ble_hs_hci_ev_pool;
static void *ble_hs_hci_os_event_buf;

static struct os_event ble_hs_event_tx_notifications = {
    .ev_type = BLE_HS_EVENT_TX_NOTIFICATIONS,
    .ev_arg = NULL,
};

static struct {
    struct os_event ev;
    int reason;
} ble_hs_event_reset = {
    .ev = {
        .ev_type = BLE_HS_EVENT_RESET,
        .ev_arg = NULL,
    },
    .reason = 0
};

static uint8_t ble_hs_synced;

#if MYNEWT_SELFTEST
/** Use a higher frequency timer to allow tests to run faster. */
#define BLE_HS_HEARTBEAT_OS_TICKS       (OS_TICKS_PER_SEC / 10)
#else
#define BLE_HS_HEARTBEAT_OS_TICKS       OS_TICKS_PER_SEC
#endif

#define BLE_HS_SYNC_RETRY_RATE          (OS_TICKS_PER_SEC / 10)    

/**
 * Handles unresponsive timeouts and periodic retries in case of resource
 * shortage.
 */
static struct os_callout_func ble_hs_heartbeat_timer;
static struct os_callout_func ble_hs_event_co;

/* Queue for host-specific OS events. */
static struct os_eventq ble_hs_evq;

/* Task structures for the host's parent task. */
static struct os_eventq *ble_hs_parent_evq;
static struct os_task *ble_hs_parent_task;

static struct os_mqueue ble_hs_rx_q;
static struct os_mqueue ble_hs_tx_q;

static struct os_mutex ble_hs_mutex;

#if BLE_HS_DEBUG
static uint8_t ble_hs_dbg_mutex_locked;
#endif

STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;
STATS_NAME_START(ble_hs_stats)
    STATS_NAME(ble_hs_stats, conn_create)
    STATS_NAME(ble_hs_stats, conn_delete)
    STATS_NAME(ble_hs_stats, hci_cmd)
    STATS_NAME(ble_hs_stats, hci_event)
    STATS_NAME(ble_hs_stats, hci_invalid_ack)
    STATS_NAME(ble_hs_stats, hci_unknown_event)
STATS_NAME_END(ble_hs_stats)

int
ble_hs_locked_by_cur_task(void)
{
    struct os_task *owner;

#if BLE_HS_DEBUG
    if (!os_started()) {
        return ble_hs_dbg_mutex_locked;
    }
#endif

    owner = ble_hs_mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/**
 * Indicates whether the host's parent task is currently running.
 */
int
ble_hs_is_parent_task(void)
{
    return !os_started() || os_sched_get_current_task() == ble_hs_parent_task;
}

void
ble_hs_lock(void)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());

#if BLE_HS_DEBUG
    if (!os_started()) {
        ble_hs_dbg_mutex_locked = 1;
        return;
    }
#endif

    rc = os_mutex_pend(&ble_hs_mutex, 0xffffffff);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

void
ble_hs_unlock(void)
{
    int rc;

#if BLE_HS_DEBUG
    if (!os_started()) {
        BLE_HS_DBG_ASSERT(ble_hs_dbg_mutex_locked);
        ble_hs_dbg_mutex_locked = 0;
        return;
    }
#endif

    rc = os_mutex_release(&ble_hs_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

void
ble_hs_process_tx_data_queue(void)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(&ble_hs_tx_q)) != NULL) {
        ble_hci_trans_hs_acl_send(om);
    }
}

void
ble_hs_process_rx_data_queue(void)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(&ble_hs_rx_q)) != NULL) {
        host_hci_acl_process(om);
    }
}

static void
ble_hs_clear_data_queue(struct os_mqueue *mqueue)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(mqueue)) != NULL) {
        os_mbuf_free_chain(om);
    }
}


static void
ble_hs_heartbeat_timer_reset(uint32_t ticks)
{
    int rc;

    rc = os_callout_reset(&ble_hs_heartbeat_timer.cf_c, ticks);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}

void
ble_hs_heartbeat_sched(int32_t ticks_from_now)
{
    if (ticks_from_now == BLE_HS_FOREVER) {
        return;
    }

    /* Reset heartbeat timer if it is not currently scheduled or if the
     * specified time is sooner than the current expiration time.
     */
    if (!os_callout_queued(&ble_hs_heartbeat_timer.cf_c) ||
        OS_TIME_TICK_LT(ticks_from_now, ble_hs_heartbeat_timer.cf_c.c_ticks)) {

        ble_hs_heartbeat_timer_reset(ticks_from_now);
    }
}

static int
ble_hs_sync(void)
{
    int rc;

    rc = ble_hs_startup_go();
    if (rc == 0) {
        ble_hs_synced = 1;
    }

    ble_hs_heartbeat_sched(BLE_HS_SYNC_RETRY_RATE);
    return rc;
}

static int
ble_hs_reset(int reason)
{
    uint16_t conn_handle;
    int rc;

    ble_hs_synced = 0;

    ble_hs_clear_data_queue(&ble_hs_tx_q);
    ble_hs_clear_data_queue(&ble_hs_rx_q);

    while (1) {
        conn_handle = ble_hs_atomic_first_conn_handle();
        if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            break;
        }

        ble_gap_conn_broken(conn_handle, reason);
    }

    rc = ble_hs_sync();
    return rc;
}

/**
 * Called once a second by the ble_hs heartbeat timer.  Handles unresponsive
 * timeouts and periodic retries in case of resource shortage.
 */
static void
ble_hs_heartbeat(void *unused)
{
    int32_t ticks_until_next;

    if (!ble_hs_synced) {
        ble_hs_sync();
        return;
    }

    /* Ensure the timer expires at least once in the next second.
     * XXX: This is not very power efficient.  We will need separate timers for
     * each module.
     */
    ticks_until_next = BLE_HS_HEARTBEAT_OS_TICKS;
    ble_hs_heartbeat_sched(ticks_until_next);

    ticks_until_next = ble_gattc_heartbeat();
    ble_hs_heartbeat_sched(ticks_until_next);

    ticks_until_next = ble_gap_heartbeat();
    ble_hs_heartbeat_sched(ticks_until_next);

    ticks_until_next = ble_l2cap_sig_heartbeat();
    ble_hs_heartbeat_sched(ticks_until_next);

    ticks_until_next = ble_sm_heartbeat();
    ble_hs_heartbeat_sched(ticks_until_next);
}

static void
ble_hs_event_handle(void *unused)
{
    struct os_callout_func *cf;
    struct os_event *ev;
    uint8_t *hci_evt;
    os_sr_t sr;
    int rc;
    int i;

    i = 0;
    while (1) {
        /* If the host has already processed several consecutive events, stop
         * and return control to the parent task.  Put an event on the parent
         * task's eventq so indicate that more host events are enqueued.
         */
        if (i >= BLE_HS_MAX_EVS_IN_A_ROW) {
            os_eventq_put(ble_hs_parent_evq, &ble_hs_event_co.cf_c.c_ev);
            break;
        }
        i++;

        OS_ENTER_CRITICAL(sr);
        ev = STAILQ_FIRST(&ble_hs_evq.evq_list);
        OS_EXIT_CRITICAL(sr);

        if (ev == NULL) {
            break;
        }

        ev = os_eventq_get(&ble_hs_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            cf = (struct os_callout_func *)ev;
            assert(cf->cf_func);
            cf->cf_func(ev->ev_arg);
            break;

        case BLE_HOST_HCI_EVENT_CTLR_EVENT:
            hci_evt = ev->ev_arg;
            rc = os_memblock_put(&ble_hs_hci_ev_pool, ev);
            BLE_HS_DBG_ASSERT_EVAL(rc == 0);

            host_hci_evt_process(hci_evt);
            break;

        case BLE_HS_EVENT_TX_NOTIFICATIONS:
            ble_gatts_tx_notifications();

        case OS_EVENT_T_MQUEUE_DATA:
            ble_hs_process_tx_data_queue();
            ble_hs_process_rx_data_queue();
            break;

        case BLE_HS_EVENT_RESET:
            BLE_HS_DBG_ASSERT(ev == &ble_hs_event_reset.ev);
            ble_hs_reset(ble_hs_event_reset.reason);
            break;

        default:
            BLE_HS_DBG_ASSERT(0);
            break;
        }
    }
}

void
ble_hs_event_enqueue(struct os_event *ev)
{
    os_eventq_put(&ble_hs_evq, ev);
    os_eventq_put(ble_hs_parent_evq, &ble_hs_event_co.cf_c.c_ev);
}

void
ble_hs_enqueue_hci_event(uint8_t *hci_evt)
{
    struct os_event *ev;
    int rc;

    ev = os_memblock_get(&ble_hs_hci_ev_pool);
    if (ev == NULL) {
        rc = ble_hci_trans_free_buf(ev->ev_arg);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    } else {
        ev->ev_queued = 0;
        ev->ev_type = BLE_HOST_HCI_EVENT_CTLR_EVENT;
        ev->ev_arg = hci_evt;
        ble_hs_event_enqueue(ev);
    }
}

/**
 * Schedules for all pending notifications and indications to be sent in the
 * host parent task.
 */
void
ble_hs_notifications_sched(void)
{
#if MYNEWT_SELFTEST
    if (!os_started()) {
        ble_gatts_tx_notifications();
        return;
    }
#endif

    ble_hs_event_enqueue(&ble_hs_event_tx_notifications);
}

void
ble_hs_sched_reset(int reason)
{
    ble_hs_event_reset.reason = reason;
    ble_hs_event_enqueue(&ble_hs_event_reset.ev);
}

/**
 * Synchronizes the host with the controller by sending a sequence of HCI
 * commands.  This function must be called before any other host functionality
 * is used, but it must be called after both the host and controller are
 * initialized.  Typically, the host-parent-task calls this function at the top
 * of its task routine.
 *
 * @return                      0 on success; nonzero on error.
 */
int
ble_hs_start(void)
{
    int rc;

    ble_hs_parent_task = os_sched_get_current_task();

    ble_hs_heartbeat_timer_reset(BLE_HS_HEARTBEAT_OS_TICKS);

    ble_gatts_start();

    rc = ble_hs_sync();
    return rc;
}

/**
 * Called when a data packet is received from the controller.  This function
 * consumes the supplied mbuf, regardless of the outcome.
 *
 * @param om                    The incoming data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_hs_rx_data(struct os_mbuf *om, void *arg)
{
    int rc;

    rc = os_mqueue_put(&ble_hs_rx_q, &ble_hs_evq, om);
    if (rc == 0) {
        os_eventq_put(ble_hs_parent_evq, &ble_hs_event_co.cf_c.c_ev);
    } else {
        os_mbuf_free_chain(om);
        rc = BLE_HS_EOS;
    }
    return rc;
}

int
ble_hs_tx_data(struct os_mbuf *om)
{
    int rc;

    rc = os_mqueue_put(&ble_hs_tx_q, &ble_hs_evq, om);
    if (rc != 0) {
        return BLE_HS_EOS;
    }
    os_eventq_put(ble_hs_parent_evq, &ble_hs_event_co.cf_c.c_ev);

    return 0;
}

static void
ble_hs_free_mem(void)
{
    free(ble_hs_hci_os_event_buf);
    ble_hs_hci_os_event_buf = NULL;
}

/**
 * Initializes the NimBLE host.  This function must be called before the OS is
 * started.  The NimBLE stack requires an application task to function.  One
 * application task in particular is designated as the "host parent task".  In
 * addition to application-specific work, the host parent task does work for
 * NimBLE by processing events generated by the host.
 *
 * @param app_evq               The event queue associated with the host parent
 *                                  task.
 * @param cfg                   The set of configuration settings to initialize
 *                                  the host with.  Specify null for defaults.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOMEM if initialization failed due to
 *                                  resource exhaustion.
 *                              Other nonzero on error.
 */
int
ble_hs_init(struct os_eventq *app_evq, struct ble_hs_cfg *cfg)
{
    int rc;

    ble_hs_free_mem();

    if (app_evq == NULL) {
        rc = BLE_HS_EINVAL;
        goto err;
    }
    ble_hs_parent_evq = app_evq;

    ble_hs_cfg_init(cfg);

    log_init();
    log_console_handler_init(&ble_hs_log_console_handler);
    log_register("ble_hs", &ble_hs_log, &ble_hs_log_console_handler);

    ble_hs_hci_os_event_buf = malloc(
        OS_MEMPOOL_BYTES(ble_hs_cfg.max_hci_bufs, sizeof (struct os_event)));
    if (ble_hs_hci_os_event_buf == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Create memory pool of OS events */
    rc = os_mempool_init(&ble_hs_hci_ev_pool, ble_hs_cfg.max_hci_bufs,
                         sizeof (struct os_event), ble_hs_hci_os_event_buf,
                         "ble_hs_hci_ev_pool");
    assert(rc == 0);

    /* Initialize eventq */
    os_eventq_init(&ble_hs_evq);

    /* Initialize stats. */
    rc = stats_module_init();
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_hci_cmd_init();

    rc = ble_hs_conn_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_svr_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_gap_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_gattc_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_gatts_init();
    if (rc != 0) {
        goto err;
    }

    os_mqueue_init(&ble_hs_rx_q, NULL);
    os_mqueue_init(&ble_hs_tx_q, NULL);

    rc = stats_init_and_reg(
        STATS_HDR(ble_hs_stats), STATS_SIZE_INIT_PARMS(ble_hs_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_hs_stats), "ble_hs");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    os_callout_func_init(&ble_hs_heartbeat_timer, ble_hs_parent_evq,
                         ble_hs_heartbeat, NULL);
    os_callout_func_init(&ble_hs_event_co, &ble_hs_evq,
                         ble_hs_event_handle, NULL);

    rc = os_mutex_init(&ble_hs_mutex);
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }
#if BLE_HS_DEBUG
    ble_hs_dbg_mutex_locked = 0;
#endif

    ble_hci_trans_set_rx_cbs_hs(host_hci_evt_rx, NULL,
                                    ble_hs_rx_data, NULL);

    return 0;

err:
    ble_hs_free_mem();
    return rc;
}
