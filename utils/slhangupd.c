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
 * Name:
 * slhangup
 *
 * Synopsis:
 * slhangup
 *
 * Description:
 * Slhangup is used to receive M_HANGUP messages sent by the slip driver.
 * This utility and extensions to the slip driver is necessary because
 * tcp and ip throws away messages that they don't understand, such as,
 * M_HANGUP.
 *
 * Example:
 * slhangup
 *
 * Author:
 * Sudji Husodo 1/31/91
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <stropts.h>
#include <termio.h>
#include <signal.h>

#include <sys/stream.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <sys/slip.h>

void unregister (int);
struct strioctl iocb;
char *slipname = "/dev/slip";
char *program;
int fd_slip;

main (int argc, char *argv[])
{
 struct strbuf ctlbuf;
 pid_t pid_sl;
 int flags = 0;

 /*
 * daemonize
 */
 program = argv[0];
 setpgrp ();

 switch (fork()) {
 case 0: break;
 case -1: perror ("fork failed"); exit (2);
 default: exit (0);
 }

 /*
 * open the slip driver
 */

 if ((fd_slip = open (slipname, O_RDWR)) < 0)
 slerror ("%s: open %s failed", argv[0], slipname);

 iocb.ic_cmd = REG_SLHUP;
 iocb.ic_timout = 0;
 iocb.ic_len = 0;
 iocb.ic_dp = "";

 /*
 * register the process, so the slip driver knows that there is
 * hangup daemon waiting to receive M_HANGUP messages.
 */

 if (ioctl (fd_slip, I_STR, &iocb) < 0)
 slerror ("%s: can't register slip's hangup daemon", argv[0]);

 signal (SIGINT, unregister);
 signal (SIGQUIT, unregister);
 signal (SIGTERM, unregister);

 /*
 * wait for any message sent by slip, if getmsg completes succesfully,
 * send a hangup signal to the slattach process id received in the
 * message.
 */

 ctlbuf.maxlen = sizeof (pid_t);
 ctlbuf.len = 0;
 ctlbuf.buf = (char *) &pid_sl;

 while (1) {
 if (getmsg (fd_slip, &ctlbuf, NULL, &flags) < 0) {
 fprintf (stderr, "\n%s: getmsg returns an error\n", program);
 continue;
 }
 fprintf (stderr, "\n%s: got M_HANGUP from slip, sending SIGUP to pid %d ...\n", program, pid_sl);

 if (kill (pid_sl, SIGHUP) < 0) {
 fprintf (stderr,"%s: can't send SIGHUP to pid %d\n",program,pid_sl);
 continue;
 }
 sleep (1);
 }
}

/*
 * slerror ()
 */

slerror (s1, s2, s3)
char *s1, *s2, *s3;
{
 fprintf (stderr,s1,s2,s3);
 fprintf (stderr,"\n");
 exit (1);
}

/*
 * unregister - unregister if the slip hangup daemon is terminated.
 */

void unregister (int x)
{
 fprintf (stderr, "\n%s: killed ...\n", program);
 iocb.ic_cmd = UNREG_SLHUP;
 if (ioctl (fd_slip, I_STR, &iocb) < 0)
 slerror ("%s: can't unregister slip's hangup daemon", program);
 exit (0);
}
