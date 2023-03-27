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
 * Header file for Intel Unix System V/386 Release 4.0 SLIP streams driver.
 */

typedef struct {
 u_int state; /* state of the entry */
 u_int sap; /* service access point */
 queue_t *qtop; /* upper streams read queue */
 queue_t *qbot; /* lower streams write queue */
 u_int qt_blocked; /* blocked upper write service flag */
 pid_t pid; /* process id of application */
 u_char *buf; /* incoming packet buffer */
 u_int inlen; /* length of captured data */
 short escape; /* flag if an ESC is detected */
 short overrun; /* flag if incoming data exceeds SLIPMTU */
 short flags; /* flag to be set (read/write) by user */
 u_char uname[IFNAMSIZ]; /* slip interface unit name */
 struct ifstats stats; /* slip interface statistics */
} slip_t;

/* The following defines is taken from RFC 1005 */

#define SLIPMTU 1006 /* maximum slip packet size */

#define END 0300 /* frame end character */
#define ESC 0333 /* frame escape character */
#define ESC_END 0334 /* transposed frame end */
#define ESC_ESC 0335 /* transposed froam esc */

/* the following are definitions for slip special I_STR ioctl */

#define REG_SLHUP 1 /* i_str ioctl to register slip hangup daemon */
#define UNREG_SLHUP 2 /* i_str ioctl to unregister sl hangup daemon */
