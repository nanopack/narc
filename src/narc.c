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

/*====================== Hash table type implementation  ==================== */

/* This is an hash table type that uses the SDS dynamic strings library as
 * keys and redis objects as values (objects can hold SDS strings,
 * lists, sets). */

void
dictVanillaFree(void *privdata, void *val)
{
	DICT_NOTUSED(privdata);
	zfree(val);
}

void
dictListDestructor(void *privdata, void *val)
{
	DICT_NOTUSED(privdata);
	listRelease((list*)val);
}

int
dictSdsKeyCompare(void *privdata, const void *key1, const void *key2)
{
	int l1,l2;
	DICT_NOTUSED(privdata);

	l1 = sdslen((sds)key1);
	l2 = sdslen((sds)key2);
	if (l1 != l2) return 0;
	return memcmp(key1, key2, l1) == 0;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1, const void *key2)
{
	DICT_NOTUSED(privdata);

	return strcasecmp(key1, key2) == 0;
}

void
dictSdsDestructor(void *privdata, void *val)
{
	DICT_NOTUSED(privdata);

	sdsfree(val);
}

unsigned int
dictSdsCaseHash(const void *key)
{
	return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

/* Stream table. sds string -> stream struct pointer. */
dictType streamTableDictType = {
	dictSdsCaseHash,	/* hash function */
	NULL,			/* key dup */
	NULL,			/* val dup */
	dictSdsKeyCaseCompare,	/* key compare */
	dictSdsDestructor,	/* key destructor */
	NULL			/* val destructor */
};

/*=========================== Server initialization ========================= */

void
initServerConfig(void)
{
	getRandomHexChars(server.runid, NARC_RUN_ID_SIZE);
	server.configfile = NULL;
	server.runid[NARC_RUN_ID_SIZE] = '\0';
	server.pidfile = zstrdup(NARC_DEFAULT_PIDFILE);
	server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
	server.host = zstrdup(NARC_DEFAULT_HOST);
	server.port = NARC_DEFAULT_PORT;
	server.protocol = zstrdup(NARC_DEFAULT_PROTO);
	server.verbosity = NARC_DEFAULT_VERBOSITY;
	server.daemonize = NARC_DEFAULT_DAEMONIZE;
	server.logfile = zstrdup(NARC_DEFAULT_LOGFILE);
	server.syslog_enabled = NARC_DEFAULT_SYSLOG_ENABLED;
	server.syslog_ident = zstrdup(NARC_DEFAULT_SYSLOG_IDENT);
	server.syslog_facility = LOG_LOCAL0;

	/* streams table -- we initiialize it here as it is part of the
	 * initial configuration */
	server.streams = dictCreate(&streamTableDictType, NULL);
}

/* =================================== Main! ================================ */

void
narcOutOfMemoryHandler(size_t allocation_size)
{
	narcLog(NARC_WARNING, "Out Of Memory allocating %zu bytes!", allocation_size);
	narcPanic("Hooky aborting for OUT OF MEMORY");
}

int
main(int argc, char **argv)
{
	struct timeval tv;

	setlocale(LC_COLLATE,"");
	zmalloc_enable_thread_safeness();
	zmalloc_set_oom_handler(narcOutOfMemoryHandler);
	srand(time(NULL)^getpid());
	gettimeofday(&tv,NULL);
	dictSetHashFunctionSeed(tv.tv_sec^tv.tv_usec^getpid());
	initServerConfig();
	printf("Hello World\n");
}





