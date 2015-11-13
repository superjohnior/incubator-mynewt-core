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

#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include "os/os.h"
#include "ble_gap_conn.h"
#include "ble_hs_work.h"

#define BLE_HS_WORK_NUM_ENTRIES     16

uint8_t ble_hs_work_busy;

static void *ble_hs_work_entry_mem;
static struct os_mempool ble_hs_work_entry_pool;
static STAILQ_HEAD(, ble_hs_work_entry) ble_hs_work_queue;

struct ble_hs_work_entry *
ble_hs_work_entry_alloc(void)
{
    struct ble_hs_work_entry *entry;

    entry = os_memblock_get(&ble_hs_work_entry_pool);
    return entry;
}

void
ble_hs_work_enqueue(struct ble_hs_work_entry *entry)
{
    STAILQ_INSERT_TAIL(&ble_hs_work_queue, entry, bwe_next);
}

void
ble_hs_work_process_next(void)
{
    struct ble_hs_work_entry *entry;
    int rc;

    assert(!ble_hs_work_busy);

    entry = STAILQ_FIRST(&ble_hs_work_queue);
    if (entry == NULL) {
        return;
    }

    switch (entry->bwe_type) {
    case BLE_HS_WORK_TYPE_DIRECT_CONNECT:
        rc = ble_gap_conn_initiate_direct(
            entry->bwe_direct_connect.bwdc_peer_addr_type,
            entry->bwe_direct_connect.bwdc_peer_addr);
        break;

    default:
        rc = -1;
        assert(0);
        break;
    }

    if (rc == 0) {
        ble_hs_work_busy = 1;
    }

    os_memblock_put(&ble_hs_work_entry_pool, entry);
}

void
ble_hs_work_done(void)
{
    assert(ble_hs_work_busy || !g_os_started);
    ble_hs_work_busy = 0;
}

int
ble_hs_work_init(void)
{
    int rc;

    free(ble_hs_work_entry_mem);
    ble_hs_work_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_WORK_NUM_ENTRIES,
                         sizeof (struct ble_hs_work_entry)));
    if (ble_hs_work_entry_mem == NULL) {
        return ENOMEM;
    }

    rc = os_mempool_init(&ble_hs_work_entry_pool, BLE_HS_WORK_NUM_ENTRIES,
                         sizeof (struct ble_hs_work_entry),
                         ble_hs_work_entry_mem, "ble_hs_work_entry_pool");
    if (rc != 0) {
        return EINVAL; // XXX
    }

    STAILQ_INIT(&ble_hs_work_queue);

    return 0;
}