/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
struct ble_l2cap_chan;

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Request Opcode In Error            | 1                 |
 * | Attribute Handle In Error          | 2                 |
 * | Error Code                         | 1                 |
 */
#define BLE_ATT_ERROR_RSP_SZ             5
struct ble_att_error_rsp {
    uint8_t baep_req_op;
    uint16_t baep_handle;
    uint8_t baep_error_code;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Server Rx MTU                      | 2                 |
 */
#define BLE_ATT_MTU_CMD_SZ               3
struct ble_att_mtu_cmd {
    uint16_t bamc_mtu;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 */
#define BLE_ATT_FIND_INFO_REQ_SZ         5
struct ble_att_find_info_req {
    uint16_t bafq_start_handle;
    uint16_t bafq_end_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Format                             | 1                 |
 * | Information Data                   | 4 to (ATT_MTU-2)  |
 */
#define BLE_ATT_FIND_INFO_RSP_BASE_SZ       2
struct ble_att_find_info_rsp {
    uint8_t bafp_format;
    /* Followed by information data. */
};

#define BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT   1
#define BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT  2

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 * | Attribute Type                     | 2                 |
 * | Attribute Value                    | 0 to (ATT_MTU-7)  |
 */
#define BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ   7
struct ble_att_find_type_value_req {
    uint16_t bavq_start_handle;
    uint16_t bavq_end_handle;
    uint16_t bavq_attr_type;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Information Data                   | 4 to (ATT_MTU-1)  |
 */
#define BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ     1
#define BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ   4

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 * | Attribute Type                     | 2 or 16           |
 */
#define BLE_ATT_READ_TYPE_REQ_BASE_SZ   5
#define BLE_ATT_READ_TYPE_REQ_SZ_16     7
#define BLE_ATT_READ_TYPE_REQ_SZ_128    21
struct ble_att_read_type_req {
    uint16_t batq_start_handle;
    uint16_t batq_end_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Length                             | 1                 |
 * | Attribute Data List                | 2 to (ATT_MTU-2)  |
 */
#define BLE_ATT_READ_TYPE_RSP_BASE_SZ       2
struct ble_att_read_type_rsp {
    uint8_t batp_length;
};

#define BLE_ATT_READ_TYPE_ADATA_BASE_SZ     2

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 */
#define BLE_ATT_READ_REQ_SZ              3
struct ble_att_read_req {
    uint16_t barq_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Value                    | 0 to (ATT_MTU-1)  |
 */
#define BLE_ATT_READ_RSP_BASE_SZ        1

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 * | Attribute Group Type               | 2 or 16           |
 */
#define BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ  5
#define BLE_ATT_READ_GROUP_TYPE_REQ_SZ_16    7
#define BLE_ATT_READ_GROUP_TYPE_REQ_SZ_128   21
struct ble_att_read_group_type_req {
    uint16_t bagq_start_handle;
    uint16_t bagq_end_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Length                             | 1                 |
 * | Attribute Data List                | 2 to (ATT_MTU-2)  |
 */
#define BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ  2
struct ble_att_read_group_type_rsp {
    uint8_t bagp_length;
};

#define BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ   4
#define BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_16     6
#define BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_128    20

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 * | Attribute Value                    | 0 to (ATT_MTU-3)  |
 */
#define BLE_ATT_WRITE_REQ_BASE_SZ       3
struct ble_att_write_req {
    uint16_t bawq_handle;
};

#define BLE_ATT_WRITE_RSP_SZ            1

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 * | Value Offset                       | 2                 |
 * | Part Attribute Value               | 0 to (ATT_MTU-5)  |
 */
#define BLE_ATT_PREP_WRITE_CMD_BASE_SZ  5
struct ble_att_prep_write_cmd {
    uint16_t bapc_handle;
    uint16_t bapc_offset;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Flags                              | 1                 |
 */
#define BLE_ATT_EXEC_WRITE_REQ_SZ       2
struct ble_att_exec_write_req {
    uint8_t baeq_flags;
};

#define BLE_ATT_EXEC_WRITE_F_CONFIRM    0x01

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 */
#define BLE_ATT_EXEC_WRITE_RSP_SZ       1


int ble_att_error_rsp_parse(void *payload, int len,
                            struct ble_att_error_rsp *rsp);
int ble_att_error_rsp_write(void *payload, int len,
                            struct ble_att_error_rsp *rsp);
int ble_att_mtu_cmd_parse(void *payload, int len,
                          struct ble_att_mtu_cmd *cmd);
int ble_att_mtu_req_write(void *payload, int len,
                          struct ble_att_mtu_cmd *cmd);
int ble_att_mtu_rsp_write(void *payload, int len,
                          struct ble_att_mtu_cmd *cmd);
int ble_att_find_info_req_parse(void *payload, int len,
                                struct ble_att_find_info_req *req);
int ble_att_find_info_req_write(void *payload, int len,
                                struct ble_att_find_info_req *req);
int ble_att_find_info_rsp_parse(void *payload, int len,
                                struct ble_att_find_info_rsp *rsp);
int ble_att_find_info_rsp_write(void *payload, int len,
                                struct ble_att_find_info_rsp *rsp);
int ble_att_find_type_value_req_parse(void *payload, int len,
                                      struct ble_att_find_type_value_req *req);
int ble_att_find_type_value_req_write(void *payload, int len,
                                      struct ble_att_find_type_value_req *req);
int ble_att_read_req_parse(void *payload, int len,
                           struct ble_att_read_req *req);
int ble_att_read_req_write(void *payload, int len,
                           struct ble_att_read_req *req);
int ble_att_read_type_req_parse(void *payload, int len,
                                struct ble_att_read_type_req *req);
int ble_att_read_type_req_write(void *payload, int len,
                                struct ble_att_read_type_req *req);
int ble_att_read_type_rsp_parse(void *payload, int len,
                                struct ble_att_read_type_rsp *rsp);
int ble_att_read_type_rsp_write(void *payload, int len,
                                struct ble_att_read_type_rsp *rsp);
int ble_att_read_group_type_req_parse(void *payload, int len,
                                      struct ble_att_read_group_type_req *req);
int ble_att_read_group_type_req_write(void *payload, int len,
                                      struct ble_att_read_group_type_req *req);
int ble_att_read_group_type_rsp_parse(void *payload, int len,
                                      struct ble_att_read_group_type_rsp *rsp);
int ble_att_read_group_type_rsp_write(void *payload, int len,
                                      struct ble_att_read_group_type_rsp *rsp);
int ble_att_write_req_parse(void *payload, int len,
                            struct ble_att_write_req *req);
int ble_att_write_req_write(void *payload, int len,
                            struct ble_att_write_req *req);
int ble_att_write_cmd_parse(void *payload, int len,
                            struct ble_att_write_req *req);
int ble_att_write_cmd_write(void *payload, int len,
                            struct ble_att_write_req *req);
int ble_att_prep_write_req_parse(void *payload, int len,
                                 struct ble_att_prep_write_cmd *cmd);
int ble_att_prep_write_req_write(void *payload, int len,
                                 struct ble_att_prep_write_cmd *cmd);
int ble_att_prep_write_rsp_parse(void *payload, int len,
                                 struct ble_att_prep_write_cmd *cmd);
int ble_att_prep_write_rsp_write(void *payload, int len,
                                 struct ble_att_prep_write_cmd *cmd);
int ble_att_exec_write_req_parse(void *payload, int len,
                                 struct ble_att_exec_write_req *req);
int ble_att_exec_write_req_write(void *payload, int len,
                                 struct ble_att_exec_write_req *req);
int ble_att_exec_write_rsp_parse(void *payload, int len);
int ble_att_exec_write_rsp_write(void *payload, int len);
