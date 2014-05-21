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
#include "config.h"
#include "tcp_client.h"
#include "udp_client.h"

#include "zmalloc.h"	/* total memory usage aware version of malloc/free */
#include "sds.h"	/* dynamic safe strings */
#include "util.h"	/* Misc functions useful in many places */

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <syslog.h>	/* definitions for system error logging */
#include <sys/time.h>	/* time types */
#include <unistd.h>	/* standard symbolic constants and types */
#include <locale.h>	/* set program locale */
#include <string.h>	/* string operations */

/*================================= Globals ================================= */

/* Global vars */
struct narc_server server; /* server global state */

/*============================ Utility functions ============================ */

/* Low level logging. To use only for very big messages, otherwise
 * narc_log() is to prefer. */
void
narc_log_raw(int level, const char *msg)
{
	const int syslogLevelMap[] = { LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING };
	const char *c = ".-*#";
	FILE *fp;
	char buf[64];
	int rawmode = (level & NARC_LOG_RAW);
	int log_to_stdout = server.logfile[0] == '\0';

	level &= 0xff; /* clear flags */
	if (level < server.verbosity) return;

	fp = log_to_stdout ? stdout : fopen(server.logfile,"a");
	if (!fp) return;

	if (rawmode) {
		fprintf(fp,"%s",msg);
	} else {
		int off;
		struct timeval tv;

		gettimeofday(&tv,NULL);
		off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
		snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
		fprintf(fp,"[%d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
	}
	fflush(fp);

	if (!log_to_stdout) fclose(fp);
	if (server.syslog_enabled) syslog(syslogLevelMap[level], "%s", msg);
}

/* Like narc_log_raw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void
narc_log(int level, const char *fmt, ...)
{
	va_list ap;
	char msg[NARC_MAX_LOGMSG_LEN];

	if ((level&0xff) < server.verbosity) return;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	narc_log_raw(level,msg);
}

void
handle_message(char *id, char *body)
{
	char *message;
	narc_log(NARC_WARNING, "Making message");
	message = sdscatprintf(sdsempty(), "<%d%d>%s %s %s %s\n", 
				server.stream_facility, server.stream_priority,
				server.time, server.stream_id, id, body);
	narc_log(NARC_WARNING, "Finished message");

	switch (server.protocol) {
		case NARC_PROTO_UDP :
			submit_udp_message(message);
			break;
		case NARC_PROTO_TCP :
			submit_tcp_message(message);
			break;
		case NARC_PROTO_SYSLOG :
			narc_log(NARC_WARNING, "syslog is not yet implemented");
			exit(1);
			break;
	}
}

void
calculate_time(uv_timer_t* handle, int status)
{
	narc_log(NARC_WARNING, "Calculating time");
	struct timeval tv;
	gettimeofday(&tv,NULL);
	strftime(server.time,sizeof(server.time),"%b %d %T",localtime(&tv.tv_sec));
	narc_log(NARC_WARNING, "Calculated time %s",server.time);
}

void
start_timer_loop()
{
	calculate_time(NULL,1);
	uv_timer_init(server.loop,&server.time_timer);
	uv_timer_start(&server.time_timer,calculate_time,500,500);
}

/*=========================== Server initialization ========================= */

void
init_server_config(void)
{
	server.pidfile = zstrdup(NARC_DEFAULT_PIDFILE);
	server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
	server.host = zstrdup(NARC_DEFAULT_HOST);
	server.port = NARC_DEFAULT_PORT;
	server.protocol = NARC_DEFAULT_PROTO;
	server.stream_id = zstrdup(NARC_DEFAULT_STREAM_ID);
	server.stream_facility = NARC_DEFAULT_STREAM_FACILITY;
	server.stream_priority = NARC_DEFAULT_STREAM_PRIORITY;
	server.verbosity = NARC_DEFAULT_VERBOSITY;
	server.daemonize = NARC_DEFAULT_DAEMONIZE;
	server.logfile = zstrdup(NARC_DEFAULT_LOGFILE);
	server.syslog_enabled = NARC_DEFAULT_SYSLOG_ENABLED;
	server.syslog_ident = zstrdup(NARC_DEFAULT_SYSLOG_IDENT);
	server.syslog_facility = LOG_LOCAL0;
	server.max_open_attempts = NARC_DEFAULT_OPEN_ATTEMPTS;
	server.open_retry_delay = NARC_DEFAULT_OPEN_DELAY;
	server.max_connect_attempts = NARC_DEFAULT_CONNECT_ATTEMPTS;
	server.connect_retry_delay = NARC_DEFAULT_CONNECT_DELAY;
	server.rate_limit = NARC_DEFAULT_RATE_LIMIT;
	server.rate_time = NARC_DEFAULT_RATE_TIME;
	server.streams = listCreate();
}

void
init_server(void)
{
	if (server.syslog_enabled)
		openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT, server.syslog_facility);

	server.loop = uv_default_loop();

	listIter *iter;
	listNode *node;

	iter = listGetIterator(server.streams, AL_START_HEAD);
	while ((node = listNext(iter)) != NULL)
		init_stream((narc_stream *)listNodeValue(node));

	listReleaseIterator(iter);

	switch (server.protocol) {
		case NARC_PROTO_UDP :
			narc_log(NARC_WARNING, "udp is implemented");
			init_udp_client();
			break;
		case NARC_PROTO_TCP :
			narc_log(NARC_WARNING, "tcp is implemented");
			init_tcp_client();
			break;
		case NARC_PROTO_SYSLOG :
			narc_log(NARC_WARNING, "syslog is not yet implemented");
			exit(1);
			break;
	}
}

/* =================================== Main! ================================ */

void
create_pid_file(void)
{
	/* Try to write the pid file in a best-effort way. */
	FILE *fp = fopen(server.pidfile,"w");
	if (fp) {
		fprintf(fp,"%d\n",(int)getpid());
		fclose(fp);
	}
}

void
daemonize(void)
{
	int fd;

	if (fork() != 0) exit(0); /* parent exits */
	setsid(); /* create a new session */

	/* Every output goes to /dev/null. If Narc is daemonized but
	* the 'logfile' is set to 'stdout' in the configuration file
	* it will not log at all. */
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) close(fd);
	}
}

void
version(void)
{
	printf("Narc v=%s malloc=%s bits=%d",
		NARC_VERSION,
		ZMALLOC_LIB,
		sizeof(long) == 4 ? 32 : 64);
	exit(0);
}

void
usage(void)
{
	fprintf(stderr,"Usage: ./narc [/path/to/narc.conf] [options]\n");
	fprintf(stderr,"       ./narc - (read config from stdin)\n");
	fprintf(stderr,"       ./narc -v or --version\n");
	fprintf(stderr,"       ./narc -h or --help\n\n");
	fprintf(stderr,"Examples:\n");
	fprintf(stderr,"       ./narc (run the server with default conf)\n");
	fprintf(stderr,"       ./narc /etc/narc.conf\n");
	fprintf(stderr,"       ./narc --port 7777\n");
	fprintf(stderr,"       ./narc /etc/mynarc.conf --loglevel verbose\n\n");
	exit(1);
}

void
narc_out_of_memory_handler(size_t allocation_size)
{
	narc_log(NARC_WARNING, "Out Of Memory allocating %zu bytes!", allocation_size);
	narcPanic("Narc aborting for OUT OF MEMORY");
}

void
narc_set_proc_title(char *title)
{
#ifdef USE_SETPROCTITLE
	setproctitle("%s", title);
#else
	NARC_NOTUSED(title);
#endif
}

void
stop(void)
{
	narc_log(NARC_NOTICE, "Stopping");
	uv_stop(server.loop);
}

int
main(int argc, char **argv)
{
	setlocale(LC_COLLATE,"");
	zmalloc_enable_thread_safeness();
	zmalloc_set_oom_handler(narc_out_of_memory_handler);
	init_server_config();

	if (argc >= 2) {
		int j = 1; /* First option to parse in argv[] */
		sds options = sdsempty();
		char *configfile = NULL;

		/* Handle special options --help and --version */
		if (strcmp(argv[1], "-v") == 0 ||
			strcmp(argv[1], "--version") == 0) version();
		if (strcmp(argv[1], "--help") == 0 ||
			strcmp(argv[1], "-h") == 0) usage();

		/* First argument is the config file name? */
		if (argv[j][0] != '-' || argv[j][1] != '-')
			configfile = argv[j++];
		/* All the other options are parsed and conceptually appended to the
		* configuration file. For instance --port 6380 will generate the
		* string "port 6380\n" to be parsed after the actual file name
		* is parsed, if any. */
		while(j != argc) {
			if (argv[j][0] == '-' && argv[j][1] == '-') {
			/* Option name */
			if (sdslen(options)) options = sdscat(options,"\n");
				options = sdscat(options,argv[j]+2);
				options = sdscat(options," ");
			} else {
				/* Option argument */
				options = sdscatrepr(options,argv[j],strlen(argv[j]));
				options = sdscat(options," ");
			}
			j++;
		}
		load_server_config(configfile, options);
		sdsfree(options);
	} else {
		narc_log(NARC_WARNING, "Warning: no config file specified, using the default config. In order to specify a config file use %s /path/to/narc.conf", argv[0]);
	}

	if (server.daemonize) daemonize();
	init_server();
	if (server.daemonize) create_pid_file();
	narc_set_proc_title(argv[0]);

	start_timer_loop();

	narc_log(NARC_WARNING, "Narc started, version " NARC_VERSION);
	narc_log(NARC_WARNING, "Waiting for events on %d files", (int)listLength(server.streams));

	return uv_run(server.loop, UV_RUN_DEFAULT);
}
