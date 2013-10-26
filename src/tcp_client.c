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

#include "narc.h"
#include "tcp_client.h"

#include "sds.h"	/* dynamic safe strings */
#include "zmalloc.h"	/* total memory usage aware version of malloc/free */

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <unistd.h>	/* standard symbolic constants and types */
#include <uv.h>		/* Event driven programming library */

/*============================ Utility functions ============================ */

void 
free_write_req(uv_write_t *req) 
{
	sdsfree((char *)req->data);
	zfree(req->bufs);
	zfree(req);
}

/*=============================== Callbacks ================================= */

void 
handle_tcp_connect(uv_connect_t* connection, int status)
{
	narc_tcp_client *client = server.client;

	client->stream = connection->handle;

	zfree(connection);
}

void
handle_tcp_write(uv_write_t* req, int status)
{
	free_write_req(req);
}

/*=============================== Watchers ================================== */

void
start_tcp_connect(void)
{
	narc_tcp_client *client = server.client;
	uv_tcp_t 	*socket = (uv_tcp_t *)zmalloc(sizeof(uv_tcp_t));

	uv_connect_t *connect = zmalloc(sizeof(uv_connect_t));

	struct sockaddr_in dest = uv_ip4_addr(server.host, server.port);

	uv_tcp_init(server.loop, socket);
	uv_tcp_keepalive(socket, 1, 60);

	if(uv_tcp_connect(connect, socket, dest, handle_tcp_connect) == UV_OK) {
		client->socket = socket;
	}
}

/*================================== API ==================================== */

void
init_tcp_client(void)
{
	server.client = zmalloc(sizeof(narc_tcp_client));

	start_tcp_connect();
}

void
submit_tcp_message(char *message)
{
	narc_tcp_client *client = (narc_tcp_client *)server.client;
	uv_write_t 	*req    = (uv_write_t *)zmalloc(sizeof(uv_write_t));
	uv_buf_t 	buf     = uv_buf_init(message, strlen(message));

	if (uv_write(req, client->stream, &buf, 1, handle_tcp_write) == UV_OK) {
		req->data = (void *)message;
	}
}

