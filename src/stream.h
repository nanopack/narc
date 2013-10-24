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

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

typedef struct {
	char 	*id;				/* message id prefix */
	char 	*file;				/* absolute path to the file */
	int 	fd;				/* file descriptor */
	off_t 	size;				/* last known file size in bytes */
	char 	buffer[NARC_MAX_BUFF_SIZE];	/* read buffer (file content) */
	char 	line[NARC_MAX_LOGMSG_LEN + 1];	/* the current line */
	int 	index;				/* the line character index */
	int 	lock;				/* read lock to prevent resetting buffers */
	int 	attempts;			/* open attempts */
} narc_stream;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/

void	start_file_open(narc_stream *stream);
void	start_file_watcher(narc_stream *stream);
void	start_file_open_timer(narc_stream *stream);
void	start_file_stat(narc_stream *stream);
void	start_file_read(narc_stream *stream);
void	init_stream(narc_stream *stream);

#endif