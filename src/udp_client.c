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
#include "udp_client.h"

#include "sds.h"	/* dynamic safe strings */
// #include "malloc.h"	/* total memory usage aware version of malloc/free */

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <unistd.h>	/* standard symbolic constants and types */
#include <uv.h>		/* Event driven programming library */
#include <string.h>

/*============================ Utility functions ============================ */

narc_udp_client
*new_udp_client(void)
{
	narc_udp_client *client = (narc_udp_client *)malloc(sizeof(narc_udp_client));
	memset(client, 0, sizeof(narc_udp_client));
	return client;
}

void
handle_udp_read_alloc_buffer(uv_handle_t *handle, size_t len,  struct uv_buf_t *buf)
{
	buf->base = malloc(len);
	buf->len = len;
}

// uv_buf_t 
// handle_udp_read_alloc_buffer(uv_handle_t* handle, size_t size)
// {
// 	return uv_buf_init(malloc(size), size);
// }

void
handle_udp_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
	if (nread >= 0){
		narc_log(NARC_WARNING, "dropping packet: %s", buf->base);

	}else {
		narc_log(NARC_WARNING, "Udp read error: %s", 
			uv_strerror(nread));
	}
	if (buf->base)
		free(buf->base);
}

void
handle_udp_send(uv_udp_send_t* req, int status)
{
	if (status != 0){
		narc_log(NARC_WARNING, "Udp send error: %s", 
			uv_err_name(status));
	}
	uv_buf_t *buf = (uv_buf_t *)req->data;
	// narc_log(NARC_WARNING, "message again: %s", buf->base);
	sdsfree(buf->base);
	free(buf);
	free(req);
}

void
start_udp_read()
{
	narc_udp_client *client = (narc_udp_client *)server.client;
	uv_udp_recv_start(&client->socket, handle_udp_read_alloc_buffer, handle_udp_read);
}

void start_udp_bind(struct addrinfo *res);

void
handle_udp_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	if (status >= 0){
		start_udp_bind(res);
	}else{
		narc_log(NARC_WARNING, "server did not resolve: %s", server.host);
	}
}

/*=============================== Watchers ================================== */

void
start_udp_resolve(void)
{
	struct addrinfo hints;
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;
	narc_log(NARC_WARNING, "server resolving: %s", server.host);
	narc_udp_client *client = (narc_udp_client *)server.client;

	uv_getaddrinfo(server.loop, &client->resolver, handle_udp_resolved, server.host, "80", &hints);
}

void
start_udp_bind(struct addrinfo *res)
{

	narc_udp_client *client = (narc_udp_client *)server.client;

	memcpy(&client->send_addr,res->ai_addr,sizeof(res->ai_addr)),
	client->send_addr.sin_port = htons(server.port);
	narc_log(NARC_WARNING, "server resolved: '%s' to %s:%d", server.host, inet_ntoa(client->send_addr.sin_addr),ntohs(client->send_addr.sin_port));

	uv_udp_init(server.loop, &client->socket);

	struct sockaddr_in recv_addr;
	uv_ip4_addr("0.0.0.0", 0, &recv_addr);
	uv_udp_bind(&client->socket, (struct sockaddr *)&recv_addr, 0);

	client->state = NARC_UDP_BOUND;
	start_udp_read();

	uv_freeaddrinfo(res);
}

/*================================== API ==================================== */

void
init_udp_client(void)
{
	narc_udp_client *client = new_udp_client();
	client->state = NARC_UDP_INITIALIZED;

	server.client = (void *)client;
	start_udp_resolve();
}

void
clean_udp_client(void)
{
	narc_udp_client *client = (narc_udp_client *)server.client;
	// uv_udp_recv_stop((uv_udp_t *)&client->socket);
	uv_close((uv_handle_t *)&client->socket, NULL);
	// server.client = NULL;
}

void
submit_udp_message(char *message)
{
	if (server.client == NULL) {
		sdsfree(message);
		return;
	}
	narc_udp_client *client = (narc_udp_client *)server.client;
	int len = strlen(message);
	if (client->state == NARC_UDP_BOUND && len > 2) {

		// we make the packet one character less so that we aren't sending the newline character
		message[strlen(message)-1] = '\0';
		// narc_log(NARC_WARNING, "sockaddr: %d %d %d %p", client->send_addr.sin_family, client->send_addr.sin_port, client->send_addr.sin_addr.s_addr, &client->send_addr);
		uv_udp_send_t *req = (uv_udp_send_t *)malloc(sizeof(uv_udp_send_t));
		memset(req, 0, sizeof(uv_udp_send_t));
		uv_buf_t *buf = malloc(sizeof(uv_buf_t));
		memset(buf, 0, sizeof(uv_buf_t));

		*buf    = uv_buf_init(message, strlen(message));
		req->data = (void *)buf;
		// narc_udp_client *client = (narc_udp_client *)server.client;
		uv_udp_send(req, &client->socket, buf, 1, (struct sockaddr *)&client->send_addr, handle_udp_send);
	} else {
		sdsfree(message);
	}
}
