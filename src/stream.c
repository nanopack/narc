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
#include "stream.h"
#include "sds.h"	/* dynamic safe strings */

// temporary
#include "tcp_client.h"

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <unistd.h>	/* standard symbolic constants and types */
#include <uv.h>		/* Event driven programming library */
#include <string.h>	/* string operations */

/*============================ Utility functions ============================ */

int 
file_exists(char *filename)
{
	struct stat buffer;   
	return (stat(filename, &buffer) == 0);
}

void
init_buffer(char *buffer)
{
	memset(buffer, '\0', NARC_MAX_BUFF_SIZE);
}

void
init_line(char *line)
{
	memset(line, '\0', NARC_MAX_MESSAGE_SIZE);
}

void
lock_stream(narc_stream *stream)
{
	stream->lock = NARC_STREAM_LOCKED;
}

int
stream_locked(narc_stream *stream)
{
	return (stream->lock == NARC_STREAM_LOCKED);
}

void
unlock_stream(narc_stream *stream)
{
	stream->lock = NARC_STREAM_UNLOCKED;
}

int
stream_unlocked(narc_stream *stream)
{
	return (stream->lock == NARC_STREAM_UNLOCKED);
}

/*============================== Callbacks ================================= */

void
handle_file_open(uv_fs_t *req)
{
	narc_stream *stream = req->data;

	if (req->result == -1) {
		narc_log(NARC_WARNING, "Error opening %s (%d/%d): errno %d", 
			stream->file, 
			stream->attempts,
			server.max_open_attempts,
			req->errorno);

		if (stream->attempts == server.max_open_attempts)
			narc_log(NARC_WARNING, "Reached max open attempts: %s", stream->file);
		else
			start_file_open_timer(stream);
	} else {
		narc_log(NARC_WARNING, "File opened: %s", stream->file);

		stream->fd       = req->result;
		stream->attempts = 0;

		start_file_watcher(stream);
		start_file_stat(stream);
	}

	uv_fs_req_cleanup(req);
	free(req);
}

void
handle_file_open_timeout(uv_timer_t* timer, int status)
{
	start_file_open((narc_stream *)timer->data);
	free(timer);
}

void 
handle_file_change(uv_fs_event_t *handle, const char *filename, int events, int status) 
{

	narc_stream *stream = handle->data;

	narc_log(NARC_WARNING, "File changed: %s", stream->file);

	if (events == UV_CHANGE)
		start_file_stat(stream);

	else if (!file_exists(stream->file)) {
		narc_log(NARC_WARNING, "File deleted: %s, attempting to re-open", stream->file);
		start_file_open(stream);
	}
}

void
handle_file_stat(uv_fs_t* req)
{
	narc_stream *stream = req->data;
	uv_statbuf_t *stat  = req->ptr;

	if (stream->size < 0){
		lseek(stream->fd, 0, SEEK_END);
		narc_log(NARC_WARNING, "file seek #1 %s",stream->file);
	}

	if (stat->st_size < stream->size){
		lseek(stream->fd, 0, SEEK_SET);
		narc_log(NARC_WARNING, "file seek #2 %s",stream->file);
	}

	stream->size = stat->st_size;

	start_file_read(stream);

	uv_fs_req_cleanup(req);
	free(req);
}

void
handle_file_read(uv_fs_t *req)
{
	narc_stream *stream = req->data;

	if (req->result < 0)
		narc_log(NARC_WARNING, "Read error (%s): %s", stream->file, uv_strerror(uv_last_error(uv_default_loop())));

	if (req->result > 0) {
		narc_log(NARC_WARNING, "file read %s",stream->file);
		
		if (stream->index == 0)
			init_line(stream->line);

		int i;
		for (i = 0; i < req->result; i++) {
			if (stream->buffer[i] == '\n' || stream->index == NARC_MAX_MESSAGE_SIZE -1) {
				stream->line[stream->index] = '\0';
				if (stream->rate_count < server.rate_limit)
				{
					if (stream->missed_count > 0)
					{
						char str[81];
						sprintf(&str[0], "Missed %d messages %c", stream->missed_count, '\0');
						stream->rate_count++;
						start_rate_limit_timer(stream);
						handle_message(stream->id, &str[0]);
						stream->missed_count = 0;
					}
					stream->rate_count++;
					start_rate_limit_timer(stream);
					handle_message(stream->id, stream->line);
				}
				else
				{
					stream->missed_count++;
				}
				init_line(stream->line);
				stream->index = 0;
			} else {
				stream->line[stream->index] = stream->buffer[i];
				stream->index += 1;
			}
		}
	}

	unlock_stream(stream);

	if (req->result == NARC_MAX_BUFF_SIZE -1)
		start_file_read(stream);

	uv_fs_req_cleanup(req);
	free(req);
}

void
handle_rate_limit_timer(uv_timer_t* timer, int status)
{
	narc_stream *stream = timer->data;
	lock_stream(stream);
	stream->rate_count--;
	unlock_stream(stream);
	free(timer);
}

/*================================= Watchers =================================== */

void
start_file_open(narc_stream *stream)
{
	narc_log(NARC_WARNING, "opening file %s",stream->file);
	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	if (uv_fs_open(server.loop, req, stream->file, O_RDONLY, 0, handle_file_open) == UV_OK) {
		req->data = (void *)stream;
		stream->attempts += 1;
	}
}

void
start_file_watcher(narc_stream *stream)
{
	uv_fs_event_t *event = malloc(sizeof(uv_fs_event_t));
	if (uv_fs_event_init(server.loop, event, stream->file, handle_file_change, 0) == UV_OK)
		event->data = (void *)stream;
}

void
start_file_open_timer(narc_stream *stream)
{
	uv_timer_t *timer = malloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, timer) == UV_OK) {
		if (uv_timer_start(timer, handle_file_open_timeout, server.open_retry_delay, 0) == UV_OK)
			timer->data = (void *)stream;
	}
}

void
start_file_stat(narc_stream *stream)
{
	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	narc_log(NARC_WARNING, "streaming file %s",stream->file);
	if (uv_fs_stat(server.loop, req, stream->file, handle_file_stat) == UV_OK)
		req->data = (void *)stream;
}

void
start_file_read(narc_stream *stream)
{
	if (stream_locked(stream)){
		narc_log(NARC_WARNING, "file locked %s",stream->file);
		return;
	}

	narc_log(NARC_WARNING, "file not locked %s",stream->file);

	init_buffer(stream->buffer);

	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	if (uv_fs_read(server.loop, req, stream->fd, stream->buffer, sizeof(stream->buffer) -1, -1, handle_file_read) == UV_OK) {
		lock_stream(stream);
		req->data = (void *)stream;
	}
}

void
start_rate_limit_timer(narc_stream *stream)
{
	uv_timer_t *timer = malloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, timer) == UV_OK) {
		if (uv_timer_start(timer, handle_rate_limit_timer, server.rate_time, 0) == UV_OK)
			timer->data = (void *)stream;
	}
}

/*================================= API =================================== */

narc_stream
*new_stream(char *id, char *file)
{
	narc_stream *stream = malloc(sizeof(narc_stream));
	
	stream->id           = id;
	stream->file         = file;
	stream->attempts     = 0;
	stream->size         = -1;
	stream->index        = 0;
	stream->lock         = NARC_STREAM_UNLOCKED;
	stream->rate_count   = 0;
	stream->missed_count = 0;

	return stream;
}

void
free_stream(narc_stream *stream)
{
	free(stream->id);
	free(stream->file);
	free(stream);
}

void
init_stream(narc_stream *stream)
{
	start_file_open(stream);
}
