// -*- mode: c; tab-width: 8; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet
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

#include "config.h"
#include "narc.h"
#include "stream.h"

#include "sds.h"	/* dynamic safe strings */
#include "zmalloc.h"	/* total memory usage aware version of malloc/free */

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <errno.h>	/* system error numbers */
#include <syslog.h>	/* definitions for system error logging */

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

static struct {
	const char     *name;
	const int       value;
} validSyslogFacilities[] = {
	{"user",    LOG_USER},
	{"local0",  LOG_LOCAL0},
	{"local1",  LOG_LOCAL1},
	{"local2",  LOG_LOCAL2},
	{"local3",  LOG_LOCAL3},
	{"local4",  LOG_LOCAL4},
	{"local5",  LOG_LOCAL5},
	{"local6",  LOG_LOCAL6},
	{"local7",  LOG_LOCAL7},
	{NULL, 0}
};

/*-----------------------------------------------------------------------------
 * Config file parsing
 *----------------------------------------------------------------------------*/

int
yesnotoi(char *s)
{
	if (!strcasecmp(s,"yes")) return 1;
	else if (!strcasecmp(s,"no")) return 0;
	else return -1;
}

void
load_server_config_from_string(char *config)
{
	char *err = NULL;
	int linenum = 0, totlines, i;
	sds *lines;

	lines = sdssplitlen(config,strlen(config),"\n",1,&totlines);

	for (i = 0; i < totlines; i++) {
		sds *argv;
		int argc;

		linenum = i+1;
		lines[i] = sdstrim(lines[i]," \t\r\n");

		/* Skip comments and blank lines */
		if (lines[i][0] == '#' || lines[i][0] == '\0') continue;

		/* Split into arguments */
		argv = sdssplitargs(lines[i],&argc);
		if (argv == NULL) {
			err = "Unbalanced quotes in configuration line";
			goto loaderr;
		}

		/* Skip this line if the resulting command vector is empty. */
		if (argc == 0) {
			sdsfreesplitres(argv,argc);
			continue;
		}
		sdstolower(argv[0]);

		/* Execute config directives */
		if (!strcasecmp(argv[0], "daemonize") && argc == 2) {
			if ((server.daemonize = yesnotoi(argv[1])) == -1) {
				err = "argument must be 'yes' or 'no'"; goto loaderr;
			}
		} else if (!strcasecmp(argv[0], "pidfile") && argc == 2) {
			zfree(server.pidfile);
			server.pidfile = zstrdup(argv[1]);
		} else if (!strcasecmp(argv[0], "loglevel") && argc == 2) {
			if (!strcasecmp(argv[1],"debug")) server.verbosity = NARC_DEBUG;
			else if (!strcasecmp(argv[1],"verbose")) server.verbosity = NARC_VERBOSE;
			else if (!strcasecmp(argv[1],"notice")) server.verbosity = NARC_NOTICE;
			else if (!strcasecmp(argv[1],"warning")) server.verbosity = NARC_WARNING;
			else {
				err = "Invalid log level. Must be one of debug, notice, warning";
				goto loaderr;
			}
		} else if (!strcasecmp(argv[0],"logfile") && argc == 2) {
			FILE *logfp;

			zfree(server.logfile);
			server.logfile = zstrdup(argv[1]);
			if (server.logfile[0] != '\0') {
				/* Test if we are able to open the file. The server will not
				* be able to abort just for this problem later... */
				logfp = fopen(server.logfile,"a");
				if (logfp == NULL) {
					err = sdscatprintf(sdsempty(),
						"Can't open the log file: %s", strerror(errno));
					goto loaderr;
				}
				fclose(logfp);
			}
		} else if (!strcasecmp(argv[0],"syslog-enabled") && argc == 2) {
			if ((server.syslog_enabled = yesnotoi(argv[1])) == -1) {
				err = "argument must be 'yes' or 'no'"; goto loaderr;
			}
		} else if (!strcasecmp(argv[0],"syslog-ident") && argc == 2) {
			if (server.syslog_ident) zfree(server.syslog_ident);
				server.syslog_ident = zstrdup(argv[1]);
		} else if (!strcasecmp(argv[0],"syslog-facility") && argc == 2) {
			int i;

			for (i = 0; validSyslogFacilities[i].name; i++) {
				if (!strcasecmp(validSyslogFacilities[i].name, argv[1])) {
					server.syslog_facility = validSyslogFacilities[i].value;
					break;
				}
			}

			if (!validSyslogFacilities[i].name) {
				err = "Invalid log facility. Must be one of 'user' or between 'local0-local7'";
				goto loaderr;
			}
		} else if (!strcasecmp(argv[0], "remote-host") && argc == 2) {
			zfree(server.host);
			server.host = zstrdup(argv[1]);
		} else if (!strcasecmp(argv[0], "remote-port") && argc == 2) {
			server.port = atoi(argv[1]);
			if (server.port < 0 || server.port > 65535) {
				err = "Invalid port"; goto loaderr;
			}
		} else if (!strcasecmp(argv[0], "remote-proto") && argc == 2) {
			if (!strcasecmp(argv[1],"udp")) server.protocol = NARC_PROTO_UDP;
			else if (!strcasecmp(argv[1],"tcp")) server.protocol = NARC_PROTO_TCP;
			else {
				err = "Invalid protocol. Must be either udp or tcp";
				goto loaderr;
			}
		} else if (!strcasecmp(argv[0], "max-attempts") && argc == 2) {
			server.max_attempts = atoi(argv[1]);
		} else if (!strcasecmp(argv[0], "retry-delay") && argc == 2) {
			server.retry_delay = atoll(argv[1]);
		} else if (!strcasecmp(argv[0], "identifier") && argc == 2) {
			zfree(server.identifier);
			server.identifier = zstrdup(argv[1]);
		} else if (!strcasecmp(argv[0],"stream") && argc == 3) {
			char *id = sdsdup(argv[1]);
			char *file = sdsdup(argv[2]);
			narc_stream *stream = new_stream(id, file);
			listAddNodeTail(server.streams, (void *)stream);
		} else {
			err = "Bad directive or wrong number of arguments"; goto loaderr;
		}
		sdsfreesplitres(argv,argc);
	}
	sdsfreesplitres(lines,totlines);

	return;

loaderr:
	fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
	fprintf(stderr, "Reading the configuration file, at line %d\n", linenum);
	fprintf(stderr, ">>> '%s'\n", lines[i]);
	fprintf(stderr, "%s\n", err);
	exit(1);
}

/* Load the server configuration from the specified filename.
 * The function appends the additional configuration directives stored
 * in the 'options' string to the config file before loading.
 *
 * Both filename and options can be NULL, in such a case are considered
 * empty. This way load_server_config can be used to just load a file or
 * just load a string. */
void
load_server_config(char *filename, char *options)
{
	sds config = sdsempty();
	char buf[NARC_CONFIGLINE_MAX+1];

	/* Load the file content */
	if (filename) {
		FILE *fp;

		if (filename[0] == '-' && filename[1] == '\0') {
			fp = stdin;
		} else {
			if ((fp = fopen(filename,"r")) == NULL) {
				narc_log(NARC_WARNING,
					"Fatal error, can't open config file '%s'", filename);
				exit(1);
			}
		}
		while(fgets(buf,NARC_CONFIGLINE_MAX+1,fp) != NULL)
			config = sdscat(config,buf);
		if (fp != stdin) fclose(fp);
	}
	/* Append the additional options */
	if (options) {
		config = sdscat(config,"\n");
		config = sdscat(config,options);
	}
	load_server_config_from_string(config);
	sdsfree(config);
}
