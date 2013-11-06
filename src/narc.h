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

#ifndef NARC_H
#define NARC_H 

#include <syslog.h>	/* definitions for system error logging */

#if defined(__sun)
#include "solarisfixes.h"
#endif

#include "adlist.h"	/* Linked lists */
#include "version.h"	/* Version macro */

#include <uv.h>		/* Event driven programming library */

/* Error codes */
#define NARC_OK		0
#define NARC_ERR	-1

/* protocol types */
#define NARC_PROTO_UDP 		1
#define NARC_PROTO_TCP 		2
#define NARC_PROTO_SYSLOG 	3

/* Static narc configuration */
#define NARC_MAX_BUFF_SIZE 		4096
#define NARC_MAX_MESSAGE_SIZE 		1024
#define NARC_CONFIGLINE_MAX		1024
#define NARC_MAX_LOGMSG_LEN		1024	/* Default maximum length of syslog messages */
#define NARC_DEFAULT_DAEMONIZE   	0
#define NARC_DEFAULT_PIDFILE     	"/var/run/narc.pid"
#define NARC_DEFAULT_LOGFILE     	""
#define NARC_DEFAULT_SYSLOG_IDENT	"narc"
#define NARC_DEFAULT_SYSLOG_ENABLED	0
#define NARC_DEFAULT_HOST 		"127.0.0.1"
#define NARC_DEFAULT_PORT 		514
#define NARC_DEFAULT_PROTO		2
#define NARC_DEFAULT_STREAM_ID		""
#define NARC_DEFAULT_STREAM_FACILITY 	LOG_USER
#define NARC_DEFAULT_STREAM_PRIORITY	LOG_ERR
#define NARC_DEFAULT_OPEN_ATTEMPTS	2
#define NARC_DEFAULT_OPEN_DELAY		3000
#define NARC_DEFAULT_CONNECT_ATTEMPTS	2
#define NARC_DEFAULT_CONNECT_DELAY	3000

/* Log levels */
#define NARC_DEBUG		0
#define NARC_VERBOSE		1
#define NARC_NOTICE		2
#define NARC_WARNING		3
#define NARC_LOG_RAW		(1<<10)	/* Modifier to log without timestamp */
// #define NARC_DEFAULT_VERBOSITY	NARC_NOTICE
#define NARC_DEFAULT_VERBOSITY	NARC_DEBUG

/* Anti-warning macro... */
#define NARC_NOTUSED(V)	((void) V)

/* We can print the stacktrace, so our assert is defined this way: */
#define narcAssertWithInfo(_c,_o,_e)	((_e)?(void)0 : (_narcAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),_exit(1)))
#define narcAssert(_e)			((_e)?(void)0 : (_narcAssert(#_e,__FILE__,__LINE__),_exit(1)))
#define narcPanic(_e)			_narcPanic(#_e,__FILE__,__LINE__),_exit(1)

/*-----------------------------------------------------------------------------
 * Global state
 *----------------------------------------------------------------------------*/

struct narc_server {
	/* General */
	char		*pidfile;			/* PID file path */
	int		arch_bits;			/* 32 or 64 depending on sizeof(long) */
	uv_loop_t	*loop;				/* Event loop */
	/* Configuration */
	int		verbosity;			/* Loglevel in narc.conf */
	int		daemonize;			/* True if running as a daemon */
	/* Logging */
	char		*logfile;			/* Path of log file */
	int		syslog_enabled;			/* Is syslog enabled? */
	char		*syslog_ident;			/* Syslog ident */
	int		syslog_facility;		/* Syslog facility */
	/* File access */
	int 		max_open_attempts;		/* Max open attempts */
	uint64_t 	open_retry_delay;		/* Millesecond delay between attempts */
	/* Server connection */
	char		*host; 				/* Remote syslog host */
	int 		port; 				/* Remote syslog port */
	int 		protocol; 			/* Protocol to use when communicating with remote host */
	void		*client;			/* the client data pointer */
	int 		max_connect_attempts;		/* Max connect attempts */
	uint64_t	connect_retry_delay;		/* Millesecond delay between attempts */
	/* Streams */
	list		*streams;			/* Stream list */
	char 		*stream_id; 			/* prefix all messages */
	int 		stream_facility;		/* Syslog stream facility */
	int 		stream_priority;		/* Syslog stream priority */
};

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct narc_server	server;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/
/* Core functions and callbacks */
void	handle_message(char *id, char *message);
void	narc_out_of_memory_handler(size_t allocation_size);
int	main(int argc, char **argv);
void	init_server_config(void);
void	init_server(void);
void	stop(void);

/* Logging */
#ifdef __GNUC__
void	narc_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#else
void	narc_log(int level, const char *fmt, ...);
#endif
void	narc_logRaw(int level, const char *msg);

/* Git SHA1 */
char		*narc_git_sha1(void);
char		*narc_git_dirty(void);
uint64_t	narc_build_id(void);

/* Deprecated */
#if defined(__GNUC__)
void	*calloc(size_t count, size_t size) __attribute__ ((deprecated));
void	free(void *ptr) __attribute__ ((deprecated));
void	*malloc(size_t size) __attribute__ ((deprecated));
void	*realloc(void *ptr, size_t size) __attribute__ ((deprecated));
#endif

/* Debugging stuff */
void	_narcAssert(char *estr, char *file, int line);
void	_narcPanic(char *msg, char *file, int line);

#endif