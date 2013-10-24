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

/* Every time the Narc Git SHA1 or Dirty status changes only this small 
 * file is recompiled, as we access this information in all the other
 * files using this functions. */

#include <string.h>

#include "release.h"
#include "version.h"
#include "crc64.h"

char
*narc_git_sha1(void)
{
  return NARC_GIT_SHA1;
}

char
*narc_git_dirty(void)
{
  return NARC_GIT_DIRTY;
}

uint64_t
narc_build_id(void)
{
  char *buildid = NARC_VERSION NARC_BUILD_ID NARC_GIT_DIRTY NARC_GIT_SHA1;

  return crc64(0,(unsigned char*)buildid,strlen(buildid));
}
