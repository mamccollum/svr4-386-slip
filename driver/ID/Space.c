/*
 * Copyright 1991, Intel Corporation
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and
 * that both the copyright notice appear in all copies and that both
 * the copyright notice and this permission notice appear in
 * supporting documentation, and that the name of Intel Corporation
 * not be used in advertising or publicity pertaining to distribution
 * of the software without specific, written prior premission.
 *
 * COMPANY AND/OR INTEL DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO
 * EVENT SHALL COMPANY NOR INTEL BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * SLIP_NUM, the maximum number of slip interface, is the only thing that
 * can be configured for the slip streams module.
 *
 * Note that the slip hangup daemon will take an entry out of slip_data.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/slip.h>

#define SLIP_NUM 3

u_int slip_num = SLIP_NUM;
slip_t slip_data [SLIP_NUM];
