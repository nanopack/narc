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

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <syslog.h>	/* definitions for system error logging */
#include <sys/time.h>	/* time types */
#include <unistd.h>	/* standard symbolic constants and types */
#include <locale.h>	/* set program locale */

/*================================= Globals ================================= */

/* Global vars */
struct narcServer server; /* server global state */

/*============================ Utility functions ============================ */

/* Low level logging. To use only for very big messages, otherwise
 * narcLog() is to prefer. */
void
narcLogRaw(int level, const char *msg)
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

/* Like narcLogRaw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void
narcLog(int level, const char *fmt, ...)
{
	va_list ap;
	char msg[NARC_MAX_LOGMSG_LEN];

	if ((level&0xff) < server.verbosity) return;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	narcLogRaw(level,msg);
}

int 
file_exists(char *filename)
{
	struct stat buffer;   
	return (stat(filename, &buffer) == 0);
}

/*=========================== Server initialization ========================= */

void
initServerConfig(void)
{
	server.configfile = NULL;
	server.pidfile = zstrdup(NARC_DEFAULT_PIDFILE);
	server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
	server.host = zstrdup(NARC_DEFAULT_HOST);
	server.port = NARC_DEFAULT_PORT;
	server.protocol = zstrdup(NARC_DEFAULT_PROTO);
	server.identifier = zstrdup(NARC_DEFAULT_IDENTIFIER);
	server.verbosity = NARC_DEFAULT_VERBOSITY;
	server.daemonize = NARC_DEFAULT_DAEMONIZE;
	server.logfile = zstrdup(NARC_DEFAULT_LOGFILE);
	server.syslog_enabled = NARC_DEFAULT_SYSLOG_ENABLED;
	server.syslog_ident = zstrdup(NARC_DEFAULT_SYSLOG_IDENT);
	server.syslog_facility = LOG_LOCAL0;
	server.max_attempts = NARC_DEFAULT_ATTEMPTS;
	server.retry_delay = NARC_DEFAULT_DELAY;
	server.streams = listCreate();
}

void
initServer(void)
{
	if (server.syslog_enabled)
		openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT, server.syslog_facility);

	server.loop = uv_default_loop();

	listIter *iter;
	listNode *node;

	iter = listGetIterator(server.streams, AL_START_HEAD);
	while ((node = listNext(iter)) != NULL)
		openFile((narcStream *)listNodeValue(node));
}

void
openFile(narcStream *stream)
{
	uv_fs_t *req = zmalloc(sizeof(uv_fs_t));
	if (uv_fs_open(server.loop, req, stream->file, O_RDONLY, 0, onFileOpen) == UV_OK)
		req->data = (void *)stream;
}

void
onFileOpen(uv_fs_t *req)
{
	narcStream *stream = req->data;

	if (req->result == -1) {
		narcLog(NARC_WARNING, "Error opening %s (%d/%d): errno %d", 
			stream->file, 
			stream->attempts,
			server.max_attempts,
			req->errorno);

		if (stream->attempts == server.max_attempts)
			narcLog(NARC_WARNING, "Reached max open attempts: %s", stream->file);
		else {
			stream->attempts += 1;
			setOpenFileTimer(stream);
		}
	} else {
		narcLog(NARC_NOTICE, "File opened: %s", stream->file);

		stream->attempts = 0;
		stream->fd       = req->result;
		stream->size     = -1;
		stream->index    = 0;
		stream->lock     = NARC_STREAM_UNLOCKED;

		initWatcher(stream);
		statFile(stream);
	}

	uv_fs_req_cleanup(req);
	zfree(req);
}

void
initWatcher(narcStream *stream)
{
	uv_fs_event_t *event = zmalloc(sizeof(uv_fs_event_t));
	if (uv_fs_event_init(server.loop, event, stream->file, onFileChange, 0) == UV_OK)
		event->data = (void *)stream;
}

/*=========================== Callbacks and core functionality ========================= */

void
setOpenFileTimer(narcStream *stream)
{
	uv_timer_t *timer = zmalloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, timer) == UV_OK) {
		if (uv_timer_start(timer, onOpenFileTimeout, server.retry_delay, 0) == UV_OK)
			timer->data = (void *)stream;
	}
}

void
onOpenFileTimeout(uv_timer_t* timer, int status)
{
	openFile((narcStream *)timer->data);
	zfree(timer);
}

void 
onFileChange(uv_fs_event_t *handle, const char *filename, int events, int status) 
{
	narcStream *stream = handle->data;

	if (events == UV_CHANGE)
		statFile(stream);

	else if (!file_exists(stream->file)) {
		narcLog(NARC_WARNING, "File deleted: %s, attempting to re-open", stream->file);
		openFile(stream);
		zfree(handle);
	}
}

void
statFile(narcStream *stream)
{
	uv_fs_t *req = zmalloc(sizeof(uv_fs_t));
	if (uv_fs_stat(server.loop, req, stream->file, onFileStat) == UV_OK)
		req->data = (void *)stream;
}

void
onFileStat(uv_fs_t* req)
{
	narcStream *stream = req->data;
	uv_statbuf_t *stat  = req->ptr;

	if (stream->size < 0)
		lseek(stream->fd, 0, SEEK_END);

	if (stat->st_size < stream->size)
		lseek(stream->fd, 0, SEEK_SET);

	stream->size = stat->st_size;

	readFile(stream);

	uv_fs_req_cleanup(req);
	zfree(req);
}

void
readFile(narcStream *stream)
{
	if (stream->lock == NARC_STREAM_LOCKED)
		return;

	initBuffer(stream->buffer);

	uv_fs_t *req = zmalloc(sizeof(uv_fs_t));
	if (uv_fs_read(server.loop, req, stream->fd, stream->buffer, sizeof(stream->buffer) -1, -1, onFileRead) == UV_OK) {
		stream->lock = NARC_STREAM_LOCKED;
		req->data = (void *)stream;
	}
}

void
onFileRead(uv_fs_t *req)
{
	narcStream *stream = req->data;

	if (stream->index == 0)
		initLine(stream->line);

	if (req->result < 0)
		narcLog(NARC_WARNING, "Read error (%s): %s", stream->file, uv_strerror(uv_last_error(uv_default_loop())));

	if (req->result > 0) {
		for (int i = 0; i < req->result; i++) {
			if (stream->buffer[i] == '\n' || stream->index == NARC_MAX_MESSAGE_SIZE -1) {
				stream->line[stream->index] = '\0';
				handleMessage(stream->id, stream->line);
				initLine(stream->line);
				stream->index = 0;
			} else {
				stream->line[stream->index] = stream->buffer[i];
				stream->index += 1;
			}
		}
	}

	stream->lock = NARC_STREAM_UNLOCKED;

	if (req->result == NARC_MAX_BUFF_SIZE -1)
		readFile(stream);

	uv_fs_req_cleanup(req);
	zfree(req);
}

void
initBuffer(char *buffer)
{
	memset(buffer, '\0', NARC_MAX_BUFF_SIZE);
}

void
initLine(char *line)
{
	memset(line, '\0', NARC_MAX_MESSAGE_SIZE);
}

void
handleMessage(char *id, char *message)
{
	printf("%s %s\n", id, message);
}

/* =================================== Main! ================================ */

void
createPidFile(void)
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
	printf("Narc v=%s sha=%s:%d malloc=%s bits=%d build=%llx\n",
		NARC_VERSION,
		narcGitSHA1(),
		atoi(narcGitDirty()) > 0,
		ZMALLOC_LIB,
		sizeof(long) == 4 ? 32 : 64,
		(unsigned long long) narcBuildId());
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
narcOutOfMemoryHandler(size_t allocation_size)
{
	narcLog(NARC_WARNING, "Out Of Memory allocating %zu bytes!", allocation_size);
	narcPanic("Narc aborting for OUT OF MEMORY");
}

void
narcSetProcTitle(char *title)
{
#ifdef USE_SETPROCTITLE
	setproctitle("%s", title);
#else
	NARC_NOTUSED(title);
#endif
}

int
main(int argc, char **argv)
{
	setlocale(LC_COLLATE,"");
	zmalloc_enable_thread_safeness();
	zmalloc_set_oom_handler(narcOutOfMemoryHandler);
	initServerConfig();

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
		loadServerConfig(configfile, options);
		sdsfree(options);
		if (configfile)
			server.configfile = getAbsolutePath(configfile);
	} else {
		narcLog(NARC_WARNING, "Warning: no config file specified, using the default config. In order to specify a config file use %s /path/to/narc.conf", argv[0]);
	}

	if (server.daemonize) daemonize();
	initServer();
	if (server.daemonize) createPidFile();
	narcSetProcTitle(argv[0]);

	narcLog(NARC_WARNING, "Narc started, version " NARC_VERSION);
	narcLog(NARC_WARNING, "Waiting for events on %d files", (int)listLength(server.streams));

	return uv_run(server.loop, UV_RUN_DEFAULT);
}
