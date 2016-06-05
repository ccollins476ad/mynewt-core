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

#include <string.h>

#include "nimble/nimble_opt.h"
#include "host/ble_sm.h"
#include "ble_hs_priv.h"
#include "ble_sm_priv.h"

#if NIMBLE_OPT(SM_SC)

#define BLE_SM_SC_PASSKEY_BYTES     4
#define BLE_SM_SC_PASSKEY_BITS      20

/**
 * The public and private keys are stored in unions.  Some crypto functions
 * accept pointers to uint32_t; others accept pointers to uint8_t.
 */
static union {
    uint32_t u32[16];
    uint8_t u8[64];
} ble_sm_sc_pub_key;
static union {
    uint32_t u32[8];
    uint8_t u8[32];
} ble_sm_sc_priv_key;

/**
 * Whether our public-private key pair has been generated.  We generate it on
 * startup for now until we have a non-volatile storage mechanism.
 */
static uint8_t ble_sm_sc_keys_generated;

/**
 * Create some shortened names for the passkey actions so that the table is
 * easier to read.
 */
#define PKACT_NONE      BLE_SM_PKACT_NONE
#define PKACT_OOB       BLE_SM_PKACT_OOB
#define PKACT_INPUT     BLE_SM_PKACT_INPUT
#define PKACT_DISP      BLE_SM_PKACT_DISP
#define PKACT_NUMCMP    BLE_SM_PKACT_NUMCMP

/**
 * This is the initiator passkey action action dpeneding on the io
 * capabilties of both parties
 */
static const uint8_t ble_sm_sc_init_pka[5 /*resp*/ ][5 /*init */] =
{
    {PKACT_NONE,    PKACT_NONE,   PKACT_INPUT, PKACT_NONE, PKACT_INPUT},
    {PKACT_NONE,    PKACT_NUMCMP, PKACT_INPUT, PKACT_NONE, PKACT_INPUT},
    {PKACT_DISP,    PKACT_DISP,   PKACT_INPUT, PKACT_NONE, PKACT_DISP},
    {PKACT_NONE,    PKACT_NONE,   PKACT_NONE,  PKACT_NONE, PKACT_NONE},
    {PKACT_DISP,    PKACT_NUMCMP, PKACT_INPUT, PKACT_NONE, PKACT_NUMCMP},
};

/**
 * This is the responder passkey action action depending on the io
 * capabilities of both parties
 */
static const uint8_t ble_sm_sc_resp_pka[5 /*init*/ ][5 /*resp */] =
{
    {PKACT_NONE,    PKACT_NONE,   PKACT_DISP,  PKACT_NONE, PKACT_DISP},
    {PKACT_NONE,    PKACT_NUMCMP, PKACT_DISP,  PKACT_NONE, PKACT_NUMCMP},
    {PKACT_INPUT,   PKACT_INPUT,  PKACT_INPUT, PKACT_NONE, PKACT_INPUT},
    {PKACT_NONE,    PKACT_NONE,   PKACT_NONE,  PKACT_NONE, PKACT_NONE},
    {PKACT_INPUT,   PKACT_NUMCMP, PKACT_DISP,  PKACT_NONE, PKACT_NUMCMP},
};

int
ble_sm_sc_passkey_action(struct ble_sm_proc *proc)
{
    int action;

    if (proc->pair_req.oob_data_flag || proc->pair_rsp.oob_data_flag) {
        action = BLE_SM_PKACT_OOB;
    } else if (!(proc->pair_req.authreq & BLE_SM_PAIR_AUTHREQ_MITM) &&
               !(proc->pair_rsp.authreq & BLE_SM_PAIR_AUTHREQ_MITM)) {

        action = BLE_SM_PKACT_NONE;
    } else if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        action = ble_sm_sc_init_pka[proc->pair_rsp.io_cap]
                                   [proc->pair_req.io_cap];
    } else {
        action = ble_sm_sc_resp_pka[proc->pair_rsp.io_cap]
                                   [proc->pair_req.io_cap];
    }

    switch (action) {
    case BLE_SM_PKACT_NONE:
        proc->pair_alg = BLE_SM_PAIR_ALG_JW;
        break;

    case BLE_SM_PKACT_OOB:
        proc->pair_alg = BLE_SM_PAIR_ALG_OOB;
        proc->flags |= BLE_SM_PROC_F_AUTHENTICATED;
        break;

    case BLE_SM_PKACT_INPUT:
    case BLE_SM_PKACT_DISP:
        proc->pair_alg = BLE_SM_PAIR_ALG_PASSKEY;
        proc->flags |= BLE_SM_PROC_F_AUTHENTICATED;
        break;

    case BLE_SM_PKACT_NUMCMP:
        proc->pair_alg = BLE_SM_PAIR_ALG_NUMCMP;
        proc->flags |= BLE_SM_PROC_F_AUTHENTICATED;
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }

    return action;
}

static int
ble_sm_sc_ensure_keys_generated(void)
{
    int rc;

    if (!ble_sm_sc_keys_generated) {
        rc = ble_sm_gen_pub_priv(ble_sm_sc_pub_key.u32,
                                 ble_sm_sc_priv_key.u32);
        if (rc != 0) {
            return rc;
        }

        ble_sm_sc_keys_generated = 1;
    }

    return 0;
}

/* Initiator does not send a confirm when pairing algorithm is any of:
 *     o just works
 *     o numeric comparison
 * (vol. 3, part H, 2.3.5.6.2)
 */
static int
ble_sm_sc_initiator_txes_confirm(struct ble_sm_proc *proc)
{
    BLE_HS_DBG_ASSERT(proc->flags & BLE_SM_PROC_F_SC);

    return proc->pair_alg != BLE_SM_PAIR_ALG_JW &&
           proc->pair_alg != BLE_SM_PAIR_ALG_NUMCMP;
}

/* Responder does not verify the initiator's random number when pairing
 * algorithm is any of:
 *     o just works
 *     o numeric comparison
 * (vol. 3, part H, 2.3.5.6.2)
 */
static int
ble_sm_sc_responder_verifies_random(struct ble_sm_proc *proc)
{
    BLE_HS_DBG_ASSERT(proc->flags & BLE_SM_PROC_F_SC);

    return proc->pair_alg != BLE_SM_PAIR_ALG_JW &&
           proc->pair_alg != BLE_SM_PAIR_ALG_NUMCMP;
}

/**
 * Generates the Ri byte used in the confirm message.  On success, the byte is
 * written to the supplied procedure object.
 */
static int
ble_sm_sc_gen_ri(struct ble_sm_proc *proc)
{
    int byte;
    int bit;
    int rc;

    switch (proc->pair_alg) {
    case BLE_SM_PAIR_ALG_JW:
    case BLE_SM_PAIR_ALG_NUMCMP:
        proc->ri = 0;
        return 0;

    case BLE_SM_PAIR_ALG_PASSKEY:
        BLE_HS_DBG_ASSERT(proc->passkey_bits_exchanged <
                          BLE_SM_SC_PASSKEY_BITS);

        byte = proc->passkey_bits_exchanged / 8;
        bit = proc->passkey_bits_exchanged % 8;
        proc->ri = 0x80 | !!(proc->tk[byte] & (1 << bit));

        proc->passkey_bits_exchanged++;

        return 0;

    case BLE_SM_PAIR_ALG_OOB:
        rc = ble_hci_util_rand(&proc->ri, 1);
        return rc;

    default:
        BLE_HS_DBG_ASSERT(0);
        return BLE_HS_EUNKNOWN;
    }
}

void
ble_sm_sc_confirm_go(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    struct ble_sm_pair_confirm cmd;
    int rc;

    rc = ble_sm_sc_gen_ri(proc);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    rc = ble_sm_alg_f4(ble_sm_sc_pub_key.u8, proc->pub_key_peer.x,
                       ble_sm_our_pair_rand(proc), proc->ri, cmd.value);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    rc = ble_sm_pair_confirm_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_SM_PROC_STATE_RANDOM;
    }
}

static void
ble_sm_sc_gen_numcmp(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    uint8_t *pka;
    uint8_t *pkb;

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        pka = ble_sm_sc_pub_key.u8;
        pkb = proc->pub_key_peer.x;
    } else {
        pka = proc->pub_key_peer.x;
        pkb = ble_sm_sc_pub_key.u8;
    }
    res->app_status = ble_sm_alg_g2(pka, pkb, proc->randm, proc->rands,
                                    &res->passkey_action.numcmp);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
    }
}

/**
 * Advances the supplied procedure object to the next state after it is
 * completed the random state.
 */
static int
ble_sm_sc_random_advance(struct ble_sm_proc *proc)
{
    int rc;

    if (proc->pair_alg != BLE_SM_PAIR_ALG_PASSKEY ||
        proc->passkey_bits_exchanged >= BLE_SM_SC_PASSKEY_BITS) {

        proc->state = BLE_SM_PROC_STATE_DHKEY_CHECK;
    } else {
        proc->state = BLE_SM_PROC_STATE_CONFIRM;
        rc = ble_sm_gen_pair_rand(ble_sm_our_pair_rand(proc));
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

void
ble_sm_sc_random_go(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    struct ble_sm_pair_random cmd;
    uint8_t pkact;
    int rc;

    memcpy(cmd.value, ble_sm_our_pair_rand(proc), 16);

    rc = ble_sm_pair_random_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        rc = ble_sm_sc_random_advance(proc);
        if (rc != 0) {
            res->app_status = rc;
            res->enc_cb = 1;
            res->sm_err = BLE_SM_ERR_UNSPECIFIED;
            return;
        }

        pkact = ble_sm_sc_passkey_action(proc);
        if (ble_sm_pkact_state(pkact) == proc->state &&
            !(proc->flags & BLE_SM_PROC_F_IO_INJECTED)) {

            res->passkey_action.action = pkact;
            BLE_HS_DBG_ASSERT(pkact == BLE_SM_PKACT_NUMCMP);
            ble_sm_sc_gen_numcmp(proc, res);
        }
    }
}

void
ble_sm_sc_random_rx(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    uint8_t confirm_val[16];
    uint8_t ia[6];
    uint8_t ra[6];
    uint8_t pkact;
    uint8_t iat;
    uint8_t rat;
    int rc;

    if (proc->flags & BLE_SM_PROC_F_INITIATOR ||
        ble_sm_sc_responder_verifies_random(proc)) {

        BLE_HS_LOG(DEBUG, "tk=");
        ble_hs_misc_log_flat_buf(proc->tk, 32);
        BLE_HS_LOG(DEBUG, "\n");

        rc = ble_sm_alg_f4(proc->pub_key_peer.x, ble_sm_sc_pub_key.u8,
                           ble_sm_their_pair_rand(proc), proc->ri,
                           confirm_val);
        if (rc != 0) {
            res->app_status = rc;
            res->sm_err = BLE_SM_ERR_UNSPECIFIED;
            res->enc_cb = 1;
            return;
        }

        if (memcmp(proc->confirm_peer, confirm_val, 16) != 0) {
            /* Random number mismatch. */
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CONFIRM_MISMATCH);
            res->sm_err = BLE_SM_ERR_UNSPECIFIED;
            res->enc_cb = 1;
            return;
        }
    }

    /* Calculate the mac key and ltk. */
    rc = ble_sm_addrs(proc, &iat, ia, &rat, ra);
    if (rc != 0) {
        res->app_status = rc;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    rc = ble_sm_alg_f5(proc->dhkey, proc->randm, proc->rands,
                       iat, ia, rat, ra, proc->mackey, proc->ltk);
    if (rc != 0) {
        res->app_status = rc;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    /* Ensure the ltk gets persisted when the pairing procedure succeeds. */
    memcpy(proc->our_keys.ltk, proc->ltk, sizeof proc->our_keys.ltk);
    proc->our_keys.ltk_valid = 1;
    proc->our_keys.ediv = 0;
    proc->our_keys.rand_val = 0;
    proc->our_keys.ediv_rand_valid = 1;

    memcpy(proc->peer_keys.ltk, proc->ltk, sizeof proc->peer_keys.ltk);
    proc->peer_keys.ltk_valid = 1;
    proc->peer_keys.ediv = 0;
    proc->peer_keys.rand_val = 0;
    proc->peer_keys.ediv_rand_valid = 1;

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        ble_sm_sc_random_advance(proc);

        pkact = ble_sm_sc_passkey_action(proc);
        if (ble_sm_pkact_state(pkact) == proc->state &&
            !(proc->flags & BLE_SM_PROC_F_IO_INJECTED)) {

            res->passkey_action.action = pkact;
            BLE_HS_DBG_ASSERT(pkact == BLE_SM_PKACT_NUMCMP);
            ble_sm_sc_gen_numcmp(proc, res);
        } else {
            res->execute = 1;
        }
    } else {
        res->execute = 1;
    }
}

void
ble_sm_sc_public_key_go(struct ble_sm_proc *proc, struct ble_sm_result *res,
                        void *arg)
{
    struct ble_sm_public_key cmd;
    uint8_t pkact;

    res->app_status = ble_sm_sc_ensure_keys_generated();
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    memcpy(cmd.x, ble_sm_sc_pub_key.u8 + 0, 32);
    memcpy(cmd.y, ble_sm_sc_pub_key.u8 + 32, 32);
    res->app_status = ble_sm_public_key_tx(proc->conn_handle, &cmd);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    pkact = ble_sm_sc_passkey_action(proc);
    if (ble_sm_pkact_state(pkact) == BLE_SM_PROC_STATE_CONFIRM) {
        res->passkey_action.action = pkact;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_SM_PROC_STATE_CONFIRM;

        if (ble_sm_proc_can_advance(proc) &&
            !ble_sm_sc_initiator_txes_confirm(proc)) {

            res->execute = 1;
        }
    }
}

void
ble_sm_sc_public_key_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                        struct ble_sm_result *res)
{
    struct ble_sm_public_key cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    int rc;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PUBLIC_KEY_SZ);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        return;
    }

    res->app_status = ble_sm_sc_ensure_keys_generated();
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    ble_sm_public_key_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm public key cmd\n");

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_PUBLIC_KEY, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->pub_key_peer = cmd;
        rc = ble_sm_alg_gen_dhkey(proc->pub_key_peer.x,
                                  proc->pub_key_peer.y,
                                  ble_sm_sc_priv_key.u32,
                                  proc->dhkey);
        if (rc != 0) {
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_DHKEY);
            res->sm_err = BLE_SM_ERR_DHKEY;
            res->enc_cb = 1;
        } else {
            if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
                proc->state = BLE_SM_PROC_STATE_CONFIRM;

                if (ble_sm_proc_can_advance(proc) &&
                    ble_sm_sc_initiator_txes_confirm(proc)) {

                    res->execute = 1;
                }
            } else {
                res->execute = 1;
            }
        }
    }
    ble_hs_unlock();
}

static void
ble_sm_sc_dhkey_check_iocap(struct ble_sm_pair_cmd *pair_cmd,
                            uint8_t *out_iocap)
{
    out_iocap[0] = pair_cmd->io_cap;
    out_iocap[1] = pair_cmd->oob_data_flag;
    out_iocap[2] = pair_cmd->authreq;
}

void
ble_sm_sc_dhkey_check_go(struct ble_sm_proc *proc, struct ble_sm_result *res,
                         void *arg)
{
    struct ble_sm_dhkey_check cmd;
    uint8_t our_addr[6];
    uint8_t iocap[3];
    uint8_t *peer_addr;
    uint8_t peer_addr_type;
    uint8_t our_addr_type;
    int rc;

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        ble_sm_sc_dhkey_check_iocap(&proc->pair_req, iocap);
    } else {
        ble_sm_sc_dhkey_check_iocap(&proc->pair_rsp, iocap);
    }

    bls_hs_priv_copy_local_identity_addr(our_addr, &our_addr_type);

    rc = ble_sm_peer_addr(proc, &peer_addr_type, &peer_addr);
    if (rc != 0) {
        goto err;
    }

    rc = ble_sm_alg_f6(proc->mackey, ble_sm_our_pair_rand(proc),
                       ble_sm_their_pair_rand(proc), proc->tk, iocap,
                       our_addr_type, our_addr, peer_addr_type, peer_addr,
                       cmd.value);
    if (rc != 0) {
        goto err;
    }

    rc = ble_sm_dhkey_check_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        goto err;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_SM_PROC_STATE_LTK_START;
    }

    return;

err:
    res->app_status = rc;
    res->enc_cb = 1;
    res->sm_err = BLE_SM_ERR_UNSPECIFIED;
}

static void
ble_sm_dhkey_check_process(struct ble_sm_proc *proc,
                           struct ble_sm_dhkey_check *cmd,
                           struct ble_sm_result *res)
{
    uint8_t exp_value[16];
    uint8_t our_addr[6];
    uint8_t iocap[3];
    uint8_t *peer_addr;
    uint8_t peer_addr_type;
    uint8_t our_addr_type;
    uint8_t pkact;

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        ble_sm_sc_dhkey_check_iocap(&proc->pair_rsp, iocap);
    } else {
        ble_sm_sc_dhkey_check_iocap(&proc->pair_req, iocap);
    }

    bls_hs_priv_copy_local_identity_addr(our_addr, &our_addr_type);

    res->app_status = ble_sm_peer_addr(proc, &peer_addr_type, &peer_addr);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    BLE_HS_LOG(DEBUG, "tk=");
    ble_hs_misc_log_flat_buf(proc->tk, 32);
    BLE_HS_LOG(DEBUG, "\n");

    res->app_status = ble_sm_alg_f6(proc->mackey,
                                    ble_sm_their_pair_rand(proc),
                                    ble_sm_our_pair_rand(proc), proc->tk,
                                    iocap, peer_addr_type, peer_addr,
                                    our_addr_type, our_addr, exp_value);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    if (memcmp(cmd->value, exp_value, 16) != 0) {
        /* Random number mismatch. */
        res->sm_err = BLE_SM_ERR_DHKEY;
        res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_DHKEY);
        res->enc_cb = 1;
        return;
    }


    pkact = ble_sm_sc_passkey_action(proc);
    if (ble_sm_pkact_state(pkact) == proc->state) {
        proc->flags |= BLE_SM_PROC_F_ADVANCE_ON_IO;
    }

    if (ble_sm_proc_can_advance(proc)) {
        if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
            proc->state = BLE_SM_PROC_STATE_ENC_START;
        }

        res->execute = 1;
    }
}

void
ble_sm_sc_dhkey_check_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                         struct ble_sm_result *res)
{
    struct ble_sm_dhkey_check cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_DHKEY_CHECK_SZ);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    res->app_status = ble_sm_dhkey_check_parse((*om)->om_data, (*om)->om_len,
                                               &cmd);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    BLE_HS_LOG(DEBUG, "rxed sm dhkey check cmd\n");

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_DHKEY_CHECK, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
    } else {
        ble_sm_dhkey_check_process(proc, &cmd, res);
    }
    ble_hs_unlock();
}

void
ble_sm_sc_init(void)
{
    ble_sm_sc_keys_generated = 0;
}

#endif  /* NIMBLE_OPT_SM_SC */
