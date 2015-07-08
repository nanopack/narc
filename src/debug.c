/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 * Copyright (c) 2013, Andrew Bennett <andrew@pagodabox.com>
 * All rights reserved.
 * Copyright (c) 2013, Tyler Flint <tyler@pagodabox.com>
 * All rights reserved.
 *
 * Contribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Contributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Contributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Hooky nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "narc.h"
#include "sha1.h"   /* SHA1 is used for DEBUG DIGEST */
#include "crc64.h"

#include <arpa/inet.h>
#include <signal.h>

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#include <ucontext.h>
#include <fcntl.h>
#endif /* HAVE_BACKTRACE */

/* ================================= Debugging ============================== */

/* Compute the sha1 of string at 's' with 'len' bytes long.
 * The SHA1 is then xored against the string pointed by digest.
 * Since xor is commutative, this operation is used in order to
 * "add" digests relative to unordered elements.
 *
 * So digest(a,b,c,d) will be the same of digest(b,a,c,d) */
void xorDigest(unsigned char *digest, void *ptr, size_t len) {
    SHA1_CTX ctx;
    unsigned char hash[20], *s = ptr;
    int j;

    SHA1Init(&ctx);
    SHA1Update(&ctx,s,len);
    SHA1Final(hash,&ctx);

    for (j = 0; j < 20; j++)
        digest[j] ^= hash[j];
}

/* This function instead of just computing the SHA1 and xoring it
 * against digest, also perform the digest of "digest" itself and
 * replace the old value with the new one.
 *
 * So the final digest will be:
 *
 * digest = SHA1(digest xor SHA1(data))
 *
 * This function is used every time we want to preserve the order so
 * that digest(a,b,c,d) will be different than digest(b,c,d,a)
 *
 * Also note that mixdigest("foo") followed by mixdigest("bar")
 * will lead to a different digest compared to "fo", "obar".
 */
void mixDigest(unsigned char *digest, void *ptr, size_t len) {
    SHA1_CTX ctx;
    char *s = ptr;

    xorDigest(digest,s,len);
    SHA1Init(&ctx);
    SHA1Update(&ctx,digest,20);
    SHA1Final(digest,&ctx);
}

/* =========================== Crash handling  ============================== */

void _narcAssert(char *estr, char *file, int line) {
    narc_log(NARC_WARNING,"=== ASSERTION FAILED ===");
    narc_log(NARC_WARNING,"==> %s:%d '%s' is not true",file,line,estr);
#ifdef HAVE_BACKTRACE
    server.assert_failed = estr;
    server.assert_file = file;
    server.assert_line = line;
    narc_log(NARC_WARNING,"(forcing SIGSEGV to print the bug report.)");
#endif
    *((char*)-1) = 'x';
}

void _narcPanic(char *msg, char *file, int line) {
    narc_log(NARC_WARNING,"------------------------------------------------");
    narc_log(NARC_WARNING,"!!! Software Failure. Press left mouse button to continue");
    narc_log(NARC_WARNING,"Guru Meditation: %s #%s:%d",msg,file,line);
#ifdef HAVE_BACKTRACE
    narc_log(NARC_WARNING,"(forcing SIGSEGV in order to print the stack trace)");
#endif
    narc_log(NARC_WARNING,"------------------------------------------------");
    *((char*)-1) = 'x';
}
