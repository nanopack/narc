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
free_tcp_write_req(uv_write_t *req) 
{
	sdsfree((char *)req->data);
	zfree(req->bufs);
	zfree(req);
}

narc_tcp_client
*new_tcp_client(void)
{
	narc_tcp_client *client = (narc_tcp_client *)zmalloc(sizeof(narc_tcp_client));

	client->state    = NARC_TCP_INITIALIZED;
	client->socket   = NULL;
	client->stream   = NULL;
	client->attempts = 0;

	return client;
}

int
tcp_client_established(narc_tcp_client *client)
{
	return (client->state == NARC_TCP_ESTABLISHED);
}

/*=============================== Callbacks ================================= */

void 
handle_tcp_connect(uv_connect_t* connection, int status)
{
	narc_tcp_client *client = server.client;

	if (status == -1) {
		uv_close((uv_handle_t *)client->socket, (uv_close_cb)zfree);
		client->socket = NULL;
		narc_log(NARC_WARNING, "Error connecting to %s:%d (%d/%d)", 
			server.host, 
			server.port,
			client->attempts,
			server.max_connect_attempts);

		if (client->attempts == server.max_connect_attempts) {
			narc_log(NARC_WARNING, "Reached max connect attempts: %s:%d", 
				server.host, 
				server.port);
			exit(1);
		} else
			start_tcp_connect_timer();

	} else {
		narc_log(NARC_NOTICE, "Connection established: %s:%d", server.host, server.port);

		client->stream   = (uv_stream_t *)connection->handle;
		client->state    = NARC_TCP_ESTABLISHED;
		client->attempts = 0;

		start_tcp_read(client->stream);
	}

	zfree(connection);
}

void
handle_tcp_write(uv_write_t* req, int status)
{
	free_tcp_write_req(req);
}

uv_buf_t 
handle_tcp_read_alloc_buffer(uv_handle_t* handle, size_t size)
{
	return uv_buf_init(zmalloc(size), size);
}

void
handle_tcp_read(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
	if (nread >= 0)
		narc_log(NARC_WARNING, "server responded unexpectedly: %s", buf.base);

	else {
		narc_log(NARC_WARNING, "Connection dropped: %s:%d, attempting to re-connect", 
			server.host,
			server.port);
		
		narc_tcp_client *client = (narc_tcp_client *)server.client;
		uv_close((uv_handle_t *)client->socket, (uv_close_cb)zfree);
		client->socket = NULL;
		client->state = NARC_TCP_INITIALIZED;

		start_resolve();
	}

	zfree(buf.base);
}

void
handle_tcp_connect_timeout(uv_timer_t* timer, int status)
{
	start_resolve();
	zfree(timer);
}

void
handle_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	if (status >= 0){
		narc_log(NARC_WARNING, "server resolved: %s", server.host);
		start_tcp_connect(res);
	}else{
		narc_log(NARC_WARNING, "server did not resolve: %s", server.host);
	}
}

/*=============================== Watchers ================================== */

void
start_resolve(void)
{
	struct addrinfo hints;
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;
	narc_log(NARC_WARNING, "server resolving: %s", server.host);
	narc_tcp_client *client = (narc_tcp_client *)server.client;
	uv_getaddrinfo(server.loop, &client->resolver, handle_resolved, server.host, "80", &hints);
}

void
start_tcp_connect(struct addrinfo *res)
{
	narc_tcp_client *client = (narc_tcp_client *)server.client;
	uv_tcp_t 	*socket = (uv_tcp_t *)zmalloc(sizeof(uv_tcp_t));

	uv_tcp_init(server.loop, socket);
	uv_tcp_keepalive(socket, 1, 60);

	struct sockaddr_in dest = uv_ip4_addr(res->ai_addr->sa_data, server.port);

	uv_connect_t *connect = zmalloc(sizeof(uv_connect_t));
	if(uv_tcp_connect(connect, socket, dest, handle_tcp_connect) == UV_OK) {
		client->socket = socket;
		client->attempts += 1;
	}
	uv_freeaddrinfo(res);
}

void
start_tcp_read(uv_stream_t *stream)
{
	uv_read_start(stream, handle_tcp_read_alloc_buffer, handle_tcp_read);
}

void
start_tcp_connect_timer(void)
{
	uv_timer_t *timer = zmalloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, timer) == UV_OK)
		uv_timer_start(timer, handle_tcp_connect_timeout, server.connect_retry_delay, 0);
}

/*================================== API ==================================== */

void
init_tcp_client(void)
{
	server.client = (void *)new_tcp_client();

	start_resolve();
}

void
submit_tcp_message(char *message)
{
	narc_tcp_client *client = (narc_tcp_client *)server.client;

	if ( ! tcp_client_established(client) ) {
		sdsfree(message);
		return;
	}

	uv_write_t *req = (uv_write_t *)zmalloc(sizeof(uv_write_t));
	uv_buf_t buf    = uv_buf_init(message, strlen(message));

	if (uv_write(req, client->stream, &buf, 1, handle_tcp_write) == UV_OK)
		req->data = (void *)message;

}
