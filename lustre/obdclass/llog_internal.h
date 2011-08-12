/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LLOG_INTERNAL_H__
#define __LLOG_INTERNAL_H__

#include <lustre_log.h>

struct llog_process_info {
        struct llog_handle  *lpi_loghandle;
        llog_cb_t            lpi_cb;
        void                *lpi_cbdata;
        void                *lpi_catdata;
        int                  lpi_rc;
        int                  lpi_flags;
        cfs_completion_t     lpi_completion;
        const struct lu_env *lpi_env;
};

struct llog_thread_info {
        struct lu_attr           lgi_attr;
        struct lu_fid            lgi_fid;
        struct llog_logid        lgi_logid;
        struct dt_object_format  lgi_dof;
        struct llog_process_data lgi_lpd;
        struct lustre_mdt_attrs  lgi_lma_attr;

        struct lu_buf            lgi_buf;
        loff_t                   lgi_off;

        struct llog_rec_hdr      lgi_lrh;
        struct llog_rec_tail     lgi_tail;
        struct llog_logid_rec    lgi_lid;
};

extern struct lu_context_key llog_thread_key;

static inline struct llog_thread_info * llog_info(const struct lu_env *env)
{
        struct llog_thread_info *lgi;

        lgi = lu_context_key_get(&env->le_ctx, &llog_thread_key);
        LASSERT(lgi);
        return lgi;
}

int llog_info_init(void);
void llog_info_fini(void);

int llog_cat_id2handle(const struct lu_env *, struct llog_handle *,
                       struct llog_handle **, struct llog_logid *);
int class_config_parse_rec(struct llog_rec_hdr *rec, char *buf, int size);
int __llog_process(const struct lu_env *, struct llog_handle *loghandle,
                   llog_cb_t cb, void *data, void *catdata, int fork);
#endif
