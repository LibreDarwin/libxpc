/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 */

/*
 * quarantine.h -- the process quarantine SPI liblaunch uses.
 *
 * libvproc serialises a spawn's quarantine attributes before handing
 * them to launchd.  Apple publishes neither libquarantine's source nor
 * its header, so this declares only that call.
 *
 * libquarantine exports its API with a second leading underscore
 * (__qtn_proc_to_data in libquarantine.tbd), so the declaration carries
 * an asm label.  The signature is the one libvproc.c calls it with;
 * QTN_SERIALIZED_DATA_MAX is the buffer size copyfile uses for the same
 * serialised form.
 */

#ifndef __QUARANTINE_H__
#define __QUARANTINE_H__

#include <stddef.h>

typedef struct _qtn_proc *qtn_proc_t;

#define QTN_SERIALIZED_DATA_MAX	0x1060

extern int qtn_proc_to_data(qtn_proc_t qp, void *data, size_t *len)
    __asm__("__qtn_proc_to_data");

#endif /* __QUARANTINE_H__ */
