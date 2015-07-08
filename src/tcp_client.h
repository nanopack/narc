// -*- mode: c; tab-width: 8; indent-tabs-mode: 1; st-rulers: [70] -*-
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2013 Pagoda Box, Inc.  All rights reserved.
 */

#ifndef NARC_TCP
#define NARC_TCP 

#include "narc.h"
#include "sds.h"	/* dynamic safe strings */

#include <uv.h>		/* Event driven programming library */

/* connection states */
#define NARC_TCP_INITIALIZED	0
#define NARC_TCP_ESTABLISHED	1

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

typedef struct {
	int 		state;		/* connection state */
	uv_tcp_t 	*socket;	/* tcp socket */
	uv_stream_t	*stream;	/* connection stream */
	int 		attempts;	/* connection attempts */
	uv_getaddrinfo_t resolver;
} narc_tcp_client;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/

/* watchers */
void	start_resolve(void);
void	start_tcp_connect(struct addrinfo *res);
void	start_tcp_read(uv_stream_t *stream);

/* api */
void	init_tcp_client(void);
void 	submit_tcp_message(char *message);
void	start_tcp_connect_timer(void);

#endif
