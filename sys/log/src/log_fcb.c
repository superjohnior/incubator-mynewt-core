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
#ifdef FCB_PRESENT
#include <string.h>

#include <os/os.h>

#include <hal/flash_map.h>
#include <fcb/fcb.h>

#include "log/log.h"

static struct flash_area sector;
struct fcb_log {
    uint8_t fl_entries;
    struct fcb *fl_fcb;
} fcb_log;

static int
log_fcb_append(struct log *log, void *buf, int len)
{
    struct fcb *fcb;
    struct fcb_entry loc;
    struct fcb_log *fcb_log;
    int rc;

    fcb_log = (struct fcb_log *)log->l_log->log_arg;
    fcb = fcb_log->fl_fcb;

    while (1) {
        rc = fcb_append(fcb, len, &loc);
        if (rc == 0) {
            break;
        }

        if (rc != FCB_ERR_NOSPACE) {
            goto err;
        }

        if (log->l_log->log_rtr_erase && fcb_log->fl_entries) {
            rc = log->l_log->log_rtr_erase(log, fcb_log);
            if (rc) {
                goto err;
            }
            continue;
        }

        rc = fcb_rotate(fcb);
        if (rc) {
            goto err;
        }
    }

    rc = flash_area_write(loc.fe_area, loc.fe_data_off, buf, len);
    if (rc) {
        goto err;
    }

    rc = fcb_append_finish(fcb, &loc);

err:
    return (rc);
}

static int
log_fcb_read(struct log *log, void *dptr, void *buf, uint16_t offset,
  uint16_t len)
{
    struct fcb_entry *loc;
    int rc;

    loc = (struct fcb_entry *)dptr;

    if (offset + len > loc->fe_data_len) {
        len = loc->fe_data_len - offset;
    }
    rc = flash_area_read(loc->fe_area, loc->fe_data_off + offset, buf, len);
    if (rc == 0) {
        return len;
    } else {
        return 0;
    }
}

static int
log_fcb_walk(struct log *log, log_walk_func_t walk_func, void *arg)
{
    struct fcb *fcb;
    struct fcb_entry loc;
    int rc;

    rc = 0;
    fcb = ((struct fcb_log *)log->l_log->log_arg)->fl_fcb;

    memset(&loc, 0, sizeof(loc));

    while (fcb_getnext(fcb, &loc) == 0) {
        rc = walk_func(log, arg, (void *) &loc, loc.fe_data_len);
        if (rc) {
            break;
        }
    }
    return (rc);
}

static int
log_fcb_flush(struct log *log)
{

    return fcb_clear(((struct fcb_log *)log->l_log->log_arg)->fl_fcb);

}

/**
 * Copies one log entry from source fcb to destination fcb
 * @param src_fcb, dst_fcb
 * @return 0 on success; non-zero on error
 */
static int
log_fcb_copy_entry(struct log *log, struct fcb_entry *entry,
                   struct fcb *dst_fcb)
{
    struct log_entry_hdr ueh;
    char data[LOG_PRINTF_MAX_ENTRY_LEN + sizeof(ueh)];
    int dlen;
    int rc;
    struct fcb *fcb_tmp;
    uint8_t entries_tmp;

    rc = log_fcb_read(log, entry, &ueh, 0, sizeof(ueh));
    if (rc != sizeof(ueh)) {
        goto err;
    }

    dlen = min(entry->fe_data_len, LOG_PRINTF_MAX_ENTRY_LEN + sizeof(ueh));

    rc = log_fcb_read(log, entry, data, 0, dlen);
    if (rc < 0) {
        goto err;
    }
    data[rc] = '\0';

    /* Changing the fcb to be logged to be dst fcb */
    fcb_tmp = ((struct fcb_log *)log->l_log->log_arg)->fl_fcb;

    entries_tmp = ((struct fcb_log *)log->l_log->log_arg)->fl_entries;

    rc = log_fcb_handler_init(log->l_log, dst_fcb, 0);
    if (rc) {
        goto err;
    }

    rc = log_fcb_append(log, data, dlen);
    if (rc) {
        goto err;
    }

    rc = log_fcb_handler_init(log->l_log, fcb_tmp, entries_tmp);
    if (rc) {
        goto err;
    }

err:
    return (rc);
}

/**
 * Copies log entries from source fcb to destination fcb
 * @param src_fcb, dst_fcb, element offset to start copying
 * @return 0 on success; non-zero on error
 */
static int
log_fcb_copy(struct log *log, struct fcb *src_fcb, struct fcb *dst_fcb,
             uint32_t offset)
{
    struct fcb_entry entry;
    int rc;

    rc = 0;

    memset(&entry, 0, sizeof(entry));
    while (!fcb_getnext(src_fcb, &entry)) {
        if (entry.fe_elem_off < offset) {
            continue;
        }
        rc = log_fcb_copy_entry(log, &entry, dst_fcb);
        if (rc) {
            break;
        }
    }

    return (rc);
}

/**
 * Flushes the log while restoring specified number of entries
 * using image scratch
 * @param src_fcb, dst_fcb
 * @return 0 on success; non-zero on error
 */
static int
log_fcb_rtr_erase(struct log *log, void *arg)
{
    struct fcb_log *fcb_log;
    struct fcb fcb_scratch;
    struct fcb *fcb;
    const struct flash_area *ptr;
    uint32_t offset;
    int rc;

    rc = 0;
    offset = 0;
    if (!log) {
        rc = -1;
        goto err;
    }

    fcb_log = (struct fcb_log *)arg;
    fcb = fcb_log->fl_fcb;

    memset(&fcb_scratch, 0, sizeof(fcb_scratch));

    if (flash_area_open(FLASH_AREA_IMAGE_SCRATCH, &ptr)) {
        goto err;
    }
    sector = *ptr;
    fcb_scratch.f_sectors = &sector;
    fcb_scratch.f_sector_cnt = 1;
    fcb_scratch.f_magic = 0x7EADBADF;
    fcb_scratch.f_version = 0;

    rc = fcb_init(&fcb_scratch);
    if (rc) {
        goto err;
    }

    rc = fcb_clear(&fcb_scratch);
    if (rc) {
        goto err;
    }

    /* Calculate offset of n-th last entry */
    rc = fcb_offset_last_n(fcb, fcb_log->fl_entries, &offset);
    if (rc) {
        goto err;
    }

    /* Copy to scratch */
    rc = log_fcb_copy(log, fcb, &fcb_scratch, offset);
    if (rc) {
        goto err;
    }

    /* Flush log */
    rc = log_fcb_flush(log);
    if (rc) {
        goto err;
    }

    /* Copy back from scratch */
    rc = log_fcb_copy(log, &fcb_scratch, fcb, 0);

err:
    return (rc);
}

int
log_fcb_handler_init(struct log_handler *handler, struct fcb *fcb, uint8_t entries)
{
    handler->log_type = LOG_TYPE_STORAGE;
    handler->log_read = log_fcb_read;
    handler->log_append = log_fcb_append;
    handler->log_walk = log_fcb_walk;
    handler->log_flush = log_fcb_flush;
    handler->log_rtr_erase = log_fcb_rtr_erase;
    fcb_log.fl_entries = entries;
    fcb_log.fl_fcb = fcb;
    handler->log_arg = &fcb_log;

    return 0;
}

#endif
