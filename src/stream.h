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
#ifndef NARC_STREAM
#define NARC_STREAM 

#include "narc.h"
#include <uv.h>

/* Stream locking */
#define NARC_STREAM_LOCKED	1
#define NARC_STREAM_UNLOCKED	2
#define NARC_STREAM_BUFFERS	1

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

typedef struct {
	char 	*id;					/* message id prefix */
	char 	*file;					/* absolute path to the file */
	int 	fd;					/* file descriptor */
	off_t 	size;					/* last known file size in bytes */
	uv_buf_t buffer[NARC_STREAM_BUFFERS];		/* read buffer (file content) */
	char 	line[(NARC_MAX_LOGMSG_LEN + 1) * 2];	/* the current and previous lines buffer */
	char	*current_line;				/* current line */
	char	*previous_line;				/* previous line */
	int	repeat_count;				/* how many times the previous line was repeated */
	int 	index;					/* the line character index */
	int 	lock;					/* read lock to prevent resetting buffers */
	int 	attempts;				/* open attempts */
	int	rate_count;				/*  */
	int	missed_count;				/*  */
	int     message_header_size;
	int64_t offset;
} narc_stream;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/

/* watchers */
void	start_file_open(narc_stream *stream);
void	start_file_watcher(narc_stream *stream);
void	start_file_open_timer(narc_stream *stream);
void	start_file_stat(narc_stream *stream);
void	start_file_read(narc_stream *stream);
void	start_rate_limit_timer(narc_stream *stream);

/* api */
narc_stream 	*new_stream(char *id, char *file);
void		free_stream(narc_stream *stream);
void		init_stream(narc_stream *stream);

#endif
