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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
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
init_buffer(uv_buf_t buffer[])
{
	int i;
	for (i = 0; i < NARC_STREAM_BUFFERS; i++) {
		buffer[i].base = malloc(NARC_MAX_BUFF_SIZE);
		buffer[i].len = NARC_MAX_BUFF_SIZE - 1;
		memset(buffer[i].base, '\0', NARC_MAX_BUFF_SIZE);
	}
}

void
free_buffer(uv_buf_t buffer[])
{
	int i;
	for (i = 0; i < NARC_STREAM_BUFFERS; i++) {
		free(buffer[i].base);
	}
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

void
submit_message(narc_stream *stream, char *message)
{
	if (stream->rate_count < server.rate_limit) {
		if (stream->missed_count > 0) {
			char str[81];
			sprintf(&str[0], "Suppressed %d messages due to rate limiting", stream->missed_count);
			stream->rate_count++;
			start_rate_limit_timer(stream);
			handle_message(stream->id, &str[0]);
			stream->missed_count = 0;
		}
		stream->rate_count++;
		start_rate_limit_timer(stream);
		handle_message(stream->id, message);
	} else {
		stream->missed_count++;
	}
}

/*============================== Callbacks ================================= */

void
handle_file_open(uv_fs_t *req)
{
	narc_stream *stream = req->data;

	if (req->result < 0) {
		narc_log(NARC_WARNING, "Error opening %s (%d/%d): %s", 
			stream->file, 
			stream->attempts,
			server.max_open_attempts,
			uv_err_name(req->result));

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
handle_file_open_timeout(uv_timer_t* timer)
{
	narc_stream *stream = (narc_stream *)timer->data;
	// uv_timer_stop(stream->open_timer);
	uv_close((uv_handle_t *)stream->open_timer, (uv_close_cb)free);
	// free(stream->open_timer);
	stream->open_timer = NULL;
	start_file_open(stream);
}

void 
handle_file_change(uv_fs_event_t *handle, const char *filename, int events, int status) 
{

	narc_stream *stream = handle->data;

	if ((events & UV_RENAME) == UV_RENAME) {
		narc_log(NARC_WARNING, "File renamed");
		// File is being rotated
		uv_fs_t close_req;
		uv_fs_close(server.loop, &close_req, stream->fd, NULL);
		// uv_fs_event_stop(stream->fs_events);
		uv_close((uv_handle_t *)stream->fs_events, (uv_close_cb)free);
		// free(stream->fs_events);
		stream->fs_events = NULL;
		start_file_open(stream);
	} else if ((events & UV_CHANGE) == UV_CHANGE) {
		if (file_exists(stream->file)) {
			start_file_stat(stream);
		} else {
			narc_log(NARC_WARNING, "File deleted: %s, attempting to re-open", stream->file);
			uv_fs_t close_req;
			uv_fs_close(server.loop, &close_req, stream->fd, NULL);
			// uv_fs_event_stop(stream->fs_events);
			uv_close((uv_handle_t *)stream->fs_events, (uv_close_cb)free);
			// free(stream->fs_events);
			stream->fs_events = NULL;
			start_file_open(stream);
		}
	}
}

void
handle_file_stat(uv_fs_t* req)
{
	narc_stream *stream = req->data;
	if (req->result >= 0) {
		uv_stat_t *stat  = req->ptr;

		// file is initially opened
		if (stream->size < 0){
			stream->offset = stat->st_size;
		}

		// file has been truncated
		if ((long int)stat->st_size < (long int)stream->size){
			stream->offset = 0;
		}

		// does the file need to be truncated?
		if ((long int)stat->st_size > (long int)server.truncate_limit){
			stream->truncate = 1;
		}

		stream->size = stat->st_size;

		start_file_read(stream);
	} else {
		// there was an error, try things again?
		uv_fs_t close_req;
		uv_fs_close(server.loop, &close_req, stream->fd, NULL);
		// uv_fs_event_stop(stream->fs_events);
		uv_close((uv_handle_t *)stream->fs_events, (uv_close_cb)free);
		// free(stream->fs_events);
		stream->fs_events = NULL;
		start_file_open(stream);
	}

	uv_fs_req_cleanup(req);
	free(req);
}

void
handle_file_read(uv_fs_t *req)
{
	narc_stream *stream = req->data;

	if (req->result < 0)
		narc_log(NARC_WARNING, "Read error (%s): %s", stream->file, uv_err_name(req->result));

	if (req->result > 0) {
		stream->offset += req->result;
		int i;
		for (i = 0; i < req->result; i++) {
			if (stream->index == 0)
				init_line(stream->current_line);

			if (stream->buffer->base[i] == '\n' || stream->index == NARC_MAX_MESSAGE_SIZE -1) {
				stream->current_line[stream->index] = '\0';

				if (strcmp(stream->current_line, stream->previous_line) == 0 ) {
					stream->repeat_count++;
					init_line(stream->current_line);
					stream->index = 0;
					if (stream->repeat_count % 500 == 0) {
						char str[NARC_MAX_LOGMSG_LEN + 20];
						sprintf(&str[0], "Previous message repeated %d times", stream->repeat_count);
						submit_message(stream, &str[0]);
					}
					continue;
				} else if (stream->repeat_count == 1) {
					submit_message(stream, stream->previous_line);
				} else if (stream->repeat_count > 1) {
					char str[NARC_MAX_LOGMSG_LEN + 20];
					sprintf(&str[0], "Previous message repeated %d times", stream->repeat_count);
					submit_message(stream, &str[0]);
				}

				submit_message(stream, stream->current_line);
				stream->repeat_count = 0;
				
				char *tmp = stream->previous_line;
				stream->previous_line = stream->current_line;
				stream->current_line = tmp;

				stream->index = 0;
			} else {
				stream->current_line[stream->index] = stream->buffer->base[i];
				stream->index += 1;
			}
		}
	}

	if (stream->truncate == 1) {
		if (truncate(stream->file, 0) == -1) {
			narc_log(NARC_WARNING, "Truncate error (%s): %s", stream->file, strerror(errno));
		}
		stream->truncate = 0;
	}

	unlock_stream(stream);

	if (req->result == NARC_MAX_BUFF_SIZE -1)
		start_file_read(stream);

	uv_fs_req_cleanup(req);
	free(req);
}

void
handle_rate_limit_timer(uv_timer_t* timer)
{
	narc_stream *stream = timer->data;
	stream->rate_count--;
	// uv_timer_stop(timer);
	uv_close((uv_handle_t *)timer, (uv_close_cb)free);
	// free(timer);
}

/*================================= Watchers =================================== */

void
start_file_open(narc_stream *stream)
{
	narc_log(NARC_WARNING, "opening file %s", stream->file);
	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	if (uv_fs_open(server.loop, req, stream->file, O_RDONLY, 0, handle_file_open) == 0) {
		req->data = (void *)stream;
		stream->attempts += 1;
	}
}

void
start_file_watcher(narc_stream *stream)
{
	stream->fs_events = malloc(sizeof(uv_fs_event_t));
	uv_fs_event_init(server.loop, stream->fs_events);
	if (uv_fs_event_start(stream->fs_events, handle_file_change, stream->file, 0) == 0)
		stream->fs_events->data = (void *)stream;
}

void
start_file_open_timer(narc_stream *stream)
{
	stream->open_timer = malloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, stream->open_timer) == 0) {
		if (uv_timer_start(stream->open_timer, handle_file_open_timeout, server.open_retry_delay, 0) == 0)
			stream->open_timer->data = (void *)stream;
	}
}

void
start_file_stat(narc_stream *stream)
{
	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	if (uv_fs_stat(server.loop, req, stream->file, handle_file_stat) == 0)
		req->data = (void *)stream;
}

void
start_file_read(narc_stream *stream)
{
	if (stream_locked(stream)){
		return;
	}

	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	if (uv_fs_read(server.loop, req, stream->fd, stream->buffer, NARC_STREAM_BUFFERS, stream->offset, handle_file_read) == 0) {
		lock_stream(stream);
		req->data = (void *)stream;
	}
}

void
start_rate_limit_timer(narc_stream *stream)
{
	uv_timer_t *timer = malloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, timer) == 0) {
		if (uv_timer_start(timer, handle_rate_limit_timer, server.rate_time, 0) == 0)
			timer->data = (void *)stream;
	}
}

/*================================= API =================================== */

narc_stream
*new_stream(char *id, char *file)
{
	narc_stream *stream = malloc(sizeof(narc_stream));
	
	stream->id                  = id;
	stream->file                = file;
	stream->attempts            = 0;
	stream->size                = -1;
	stream->index               = 0;
	stream->lock                = NARC_STREAM_UNLOCKED;
	stream->rate_count          = 0;
	stream->missed_count        = 0;
	stream->repeat_count        = 0;
	stream->message_header_size = strlen(id) + strlen(server.stream_id) + 24;
	stream->offset              = 0;
	stream->fs_events			= NULL;
	stream->open_timer			= NULL;

	stream->current_line  = &stream->line[0];
	stream->previous_line = &stream->line[NARC_MAX_LOGMSG_LEN + 1];
	init_line(stream->current_line);
	init_line(stream->previous_line);

	init_buffer(stream->buffer);

	return stream;
}

void
stop_stream(narc_stream *stream)
{
	if (stream->fs_events != NULL) {
		// uv_fs_event_stop(stream->fs_events);
		uv_close((uv_handle_t *)stream->fs_events, (uv_close_cb)free);
		// free(stream->fs_events);
		stream->fs_events = NULL;
	}
	if (stream->open_timer != NULL) {
		// uv_timer_stop(stream->open_timer);
		uv_close((uv_handle_t *)stream->open_timer, (uv_close_cb)free);
		// free(stream->open_timer);
		stream->open_timer = NULL;
	}
}

void
free_stream(void *ptr)
{
	narc_stream *stream = (narc_stream *)ptr;
	// stop_stream(stream);
	free_buffer(stream->buffer);
	sdsfree(stream->id);
	sdsfree(stream->file);
	free(stream);
}

void
init_stream(narc_stream *stream)
{
	start_file_open(stream);
}
