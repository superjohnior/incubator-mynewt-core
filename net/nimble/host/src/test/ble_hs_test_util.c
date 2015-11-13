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

#include <string.h>
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "testutil/testutil.h"
#include "host/host_hci.h"
#include "ble_hs_ack.h"
#include "ble_gap_conn.h"
#include "ble_hs_test_util.h"

void
ble_hs_test_util_build_cmd_complete(uint8_t *dst, int len,
                                    uint8_t param_len, uint8_t num_pkts,
                                    uint16_t opcode)
{
    TEST_ASSERT(len >= BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN);

    dst[0] = BLE_HCI_EVCODE_COMMAND_COMPLETE;
    dst[1] = 5 + param_len;
    dst[2] = num_pkts;
    htole16(dst + 3, opcode);
}

void
ble_hs_test_util_build_cmd_status(uint8_t *dst, int len,
                                  uint8_t status, uint8_t num_pkts,
                                  uint16_t opcode)
{
    TEST_ASSERT(len >= BLE_HCI_EVENT_CMD_STATUS_LEN);

    dst[0] = BLE_HCI_EVCODE_COMMAND_STATUS;
    dst[1] = BLE_HCI_EVENT_CMD_STATUS_LEN;
    dst[2] = status;
    dst[3] = num_pkts;
    htole16(dst + 4, opcode);
}

void
ble_hs_test_util_create_conn(uint16_t handle, uint8_t *addr)
{
    struct hci_le_conn_complete evt;
    int rc;

    rc = ble_gap_conn_initiate_direct(0, addr);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, BLE_ERR_SUCCESS);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
}

void
ble_hs_test_util_rx_ack(uint16_t opcode, uint8_t status)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_STATUS_LEN];
    int rc;

    ble_hs_test_util_build_cmd_status(buf, sizeof buf, status, 1, opcode);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_le_ack(uint16_t ocf, uint8_t status)
{
    ble_hs_test_util_rx_ack((BLE_HCI_OGF_LE << 10) | ocf, status);
}