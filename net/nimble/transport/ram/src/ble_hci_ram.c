#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include "os/os.h"
#include "nimble/hci_transport.h"

static ble_hci_trans_rx_cmd_fn *ble_hci_ram_rx_cmd_hs_cb;
static void *ble_hci_ram_rx_cmd_hs_arg;

static ble_hci_trans_rx_cmd_fn *ble_hci_ram_rx_cmd_ll_cb;
static void *ble_hci_ram_rx_cmd_ll_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_ram_rx_acl_hs_cb;
static void *ble_hci_ram_rx_acl_hs_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_ram_rx_acl_ll_cb;
static void *ble_hci_ram_rx_acl_ll_arg;

static struct os_mempool ble_hci_ram_evt_pool;
static void *ble_hci_ram_evt_buf;

static uint8_t *ble_hci_ram_hs_cmd_buf;
static uint8_t ble_hci_ram_hs_cmd_buf_alloced;

void
ble_hci_trans_set_rx_cbs_hs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                                void *cmd_arg,
                                ble_hci_trans_rx_acl_fn *acl_cb,
                                void *acl_arg)
{
    ble_hci_ram_rx_cmd_hs_cb = cmd_cb;
    ble_hci_ram_rx_cmd_hs_arg = cmd_arg;
    ble_hci_ram_rx_acl_hs_cb = acl_cb;
    ble_hci_ram_rx_acl_hs_arg = acl_arg;
}

void
ble_hci_trans_set_rx_cbs_ll(ble_hci_trans_rx_cmd_fn *cmd_cb,
                                void *cmd_arg,
                                ble_hci_trans_rx_acl_fn *acl_cb,
                                void *acl_arg)
{
    ble_hci_ram_rx_cmd_ll_cb = cmd_cb;
    ble_hci_ram_rx_cmd_ll_arg = cmd_arg;
    ble_hci_ram_rx_acl_ll_cb = acl_cb;
    ble_hci_ram_rx_acl_ll_arg = acl_arg;
}

int
ble_hci_trans_hs_cmd_send(uint8_t *cmd)
{
    int rc;

    assert(ble_hci_ram_rx_cmd_ll_cb != NULL);

    rc = ble_hci_ram_rx_cmd_ll_cb(cmd, ble_hci_ram_rx_cmd_ll_arg);
    return rc;
}

int
ble_hci_trans_ll_evt_send(uint8_t *hci_ev)
{
    int rc;

    assert(ble_hci_ram_rx_cmd_hs_cb != NULL);

    rc = ble_hci_ram_rx_cmd_hs_cb(hci_ev, ble_hci_ram_rx_cmd_hs_arg);
    return rc;
}

int
ble_hci_trans_hs_acl_send(struct os_mbuf *om)
{
    int rc;

    assert(ble_hci_ram_rx_acl_ll_cb != NULL);

    rc = ble_hci_ram_rx_acl_ll_cb(om, ble_hci_ram_rx_acl_ll_arg);
    return rc;
}

int
ble_hci_trans_ll_acl_send(struct os_mbuf *om)
{
    int rc;

    assert(ble_hci_ram_rx_acl_hs_cb != NULL);

    rc = ble_hci_ram_rx_acl_hs_cb(om, ble_hci_ram_rx_acl_hs_arg);
    return rc;
}

uint8_t *
ble_hci_trans_alloc_buf(int type)
{
    uint8_t *buf;

    switch (type) {
    case BLE_HCI_TRANS_BUF_EVT_LO:
    case BLE_HCI_TRANS_BUF_EVT_HI:
        buf = os_memblock_get(&ble_hci_ram_evt_pool);
        break;

    case BLE_HCI_TRANS_BUF_CMD:
        assert(!ble_hci_ram_hs_cmd_buf_alloced);
        ble_hci_ram_hs_cmd_buf_alloced = 1;
        buf = ble_hci_ram_hs_cmd_buf;
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

    if (buf == ble_hci_ram_hs_cmd_buf) {
        assert(ble_hci_ram_hs_cmd_buf_alloced);
        ble_hci_ram_hs_cmd_buf_alloced = 0;
        rc = 0;
    } else {
        rc = os_memblock_put(&ble_hci_ram_evt_pool, buf);
    }

    return rc;
}

static void
ble_hci_ram_free_mem(void)
{
    free(ble_hci_ram_evt_buf);
    ble_hci_ram_evt_buf = NULL;

    free(ble_hci_ram_hs_cmd_buf);
    ble_hci_ram_hs_cmd_buf = NULL;
    ble_hci_ram_hs_cmd_buf_alloced = 0;
}

int
ble_hci_ram_init(int num_evt_bufs, int buf_size)
{
    int rc;

    ble_hci_ram_free_mem();

    ble_hci_ram_evt_buf = malloc(OS_MEMPOOL_BYTES(num_evt_bufs, buf_size));
    if (ble_hci_ram_evt_buf == NULL) {
        rc = ENOMEM;
        goto err;
    }

    /* Create memory pool of command buffers */
    rc = os_mempool_init(&ble_hci_ram_evt_pool, num_evt_bufs,
                         buf_size, ble_hci_ram_evt_buf,
                         "ble_hci_ram_evt_pool");
    if (rc != 0) {
        rc = EINVAL;
        goto err;
    }

    ble_hci_ram_hs_cmd_buf = malloc(BLE_HCI_TRANS_CMD_SZ);
    if (ble_hci_ram_hs_cmd_buf == NULL) {
        rc = ENOMEM;
        goto err;
    }

    return 0;

err:
    ble_hci_ram_free_mem();
    return rc;
}
