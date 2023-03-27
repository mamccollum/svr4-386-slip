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
 * slattach
 *
 * Synopsis:
 * slattach [-i] nodename interface_name
 * slattach -d [-i] devname interface_name [ baudrate ]
 * slattach - interface_name process_id
 *
 * Description:
 * Slattach is used to assign a serial line to a network interface
 * using the Internet Protocol.
 *
 * Slattach defaults to use BNU (Basic Networking Utilities) to
 * establish an outgoing serial line connection. Look at Advanced
 * System Administration Section 7-15. The -d option causes slattach
 * to directly open the specified device name, without using BNU, to be
 * linked to the slip driver. In this case the baudrate parameter is
 * used to set the speed of the connection; the default speed is 9600.
 *
 * The - options specifies that stdin is the device to be attached to
 * the slip driver.
 *
 * If the slip hangup daemon (slhangupd) is run, slattach by default
 * is set to receive hangup signal (SIGHUP) sent by the slip driver
 * through slhangupd. The -i option ignores any hangup signal.
 *
 * Example:
 * slattach [-i] venus sl0
 * slattach -d [-i] /dev/tty00 sl0 19200
 * slattach - sl0
 *
 * Author:
 * Sudji Husodo 1/31/91
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stropts.h>
#include <termio.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stream.h>
#include <net/if.h>
#include <sys/slip.h>
/*
#include <dial.h>
*/

/*
 * Unix V.4.0.3 doesn't include dial.h in /usr/include directory,
 * so define typedef CALL.
 */

typedef struct {
 struct termio *attr; /* ptr to termio attribute struct */
 int baud; /* unused */
 int speed; /* less than 0 for any speed */
 char *line; /* device name for out-going line */
 char *telno; /* ptr to tel-no/system name string */
 int modem; /* unused */
 char *device; /* unused */
 int dev_len; /* unused */
} CALL;

char *program;
char *ipname = "/dev/ip";
char *slipname = "/dev/slip";
char devname[16], ifname[16];
int fd_link, fd_ip, fd_slip;
unsigned char fd_dev;
int ppid = 0;

CALL ds;
struct termio tio;
void slsignal (int);

main (int argc, char *argv[])
{
 int iflag = 0; /* ignore hangup signal flag */
 int dflag = 0;
 extern char *optarg;
 extern int optind;
 int ac;
 char **av;

 int speed;
 pid_t pid;
 struct strioctl iocb;
 struct ifreq ifr;

#ifdef DEBUG
 extern int Debug;
 Debug = 9;
#endif

 program = argv[0];

 /*
 * If first argument is '-' only, we're using stdin as the device
 */

 if (argv[1][0] == '-' && argv[1][1] == '\0') {
 if (strlen(argv[2]) < 1) /* check for usage */
 usage ();
 }

 /*
 * otherwise check for options using getopt
 */

 else {
 while ((speed = getopt (argc, argv, "id")) != EOF)
 switch (speed) {
 case 'i': iflag++; break;
 case 'd': dflag++; break;
 case '?': usage ();
 }

 ac = argc - optind + 1; /* set ac, argument count */
 av = &argv [optind]; /* and av, arguments */

 if (ac < 3) /* check legal usage */
 usage ();
 }

 /*
 * daemonize the process if it is not a DEBUG version
 */

#ifndef DEBUG
 setpgrp ();

 switch (fork()) {
 case 0:
 break;
 case -1:
 perror ("fork failed");
 exit (2);
 default:
 if (argv[1][0] == '-' && argv[1][1] == '\0')
 sleep (3);
 else
 wait (0);
 exit (0);
 }
#endif

 /*
 * If first argument is '-' only, we're using stdin as the device
 */

 if (argv[1][0] == '-' && argv[1][1] == '\0') {
 fd_dev = fileno (stdin); /* device is stdin */
 strcpy (ifname, argv[2]); /* remember ifname */
 ppid = atoi (argv[3]);

 ioctl (fd_dev, I_POP, "ldterm"); /* ignore ioctl error */
 ioctl (fd_dev, I_POP, "ttcompat");

 if (ioctl (fd_dev, TCGETA, &tio) < 0)
 slerror ("%s: ioctl TCGETA failed", argv[0]);

 tio.c_cflag |= CS8 | CREAD;

 if (ioctl (fd_dev, TCSETA, &tio) < 0)
 slerror ("%s: ioctl TCSANOW failed", argv[0]);
 }

 /*
 * We are using BNU (default) through dial(3C)
 */

 else if (!dflag) {
 strcpy (devname, av[0]); /* get nodename and ifname */
 strcpy (ifname, av[1]);

 /*
 * get device file descriptor through BNU's dial call.
 * get and set device attributes. pop ldterm and ttcompat.
 */

 ds.attr = 0;
 ds.speed = -1;
 ds.line = "";
 ds.telno = devname;

 if ((fd_dev = dial (ds)) < 0)
 slerror ("%s: dial failed", argv[0]);

 if (ioctl (fd_dev, TCGETA, &tio) < 0)
 slerror ("%s: ioctl TCGETA failed", argv[0]);

 tio.c_iflag = BRKINT | IGNPAR;
 tio.c_cflag = (tio.c_cflag & 0x0f) | CS8 | CREAD;

 if (ioctl (fd_dev, TCSETA, &tio) < 0)
 slerror ("%s: ioctl TCSANOW failed", argv[0]);

 ioctl (fd_dev, I_POP, "ldterm"); /* ignore ioctl error */
 ioctl (fd_dev, I_POP, "ttcompat");
 }

 /*
 * we are not using BNU. The next argument is the serial device name.
 */

 else {
 if (*av[0] == '/') { /* if device path is absolute */
 strcpy (devname, av[0]); /* copy device as it is */
 }
 else { /* device path is relative */
 strcpy (devname, "/dev/"); /* add "/dev/" to devname */
 strcat (devname, av[0]);
 }
 strcpy (ifname, av[1]); /* the next two argument is */
 speed = atoi (av[2]); /* ifname and baud (optional) */

 /*
 * open device directly. pop ldterm and ttcompat.
 * get and set device attributes.
 */

 if ((fd_dev = open (devname, O_RDWR)) < 0)
 slerror ("%s: open %s failed", argv[0], devname);

 ioctl (fd_dev, I_POP, "ldterm"); /* ignore ioctl error */
 ioctl (fd_dev, I_POP, "ttcompat");

 if (ioctl (fd_dev, TCGETA, &tio) < 0)
 slerror ("%s: ioctl TCGETA failed", argv[0]);

 tio.c_iflag = BRKINT | IGNPAR; /* sig break, no parity */
 tio.c_cflag = CS8 | CREAD; /* 8 bits, enable rcver */

 switch (speed) {
 case 1200: tio.c_cflag |= B1200; break;
 case 1800: tio.c_cflag |= B1800; break;
 case 2400: tio.c_cflag |= B2400; break;
 case 4800: tio.c_cflag |= B4800; break;
 case 19200: tio.c_cflag |= B19200; break;
 case 38400: tio.c_cflag |= B38400; break;
 default: tio.c_cflag |= B9600; break;
 }

 if (ioctl (fd_dev, TCSETA, &tio) < 0)
 slerror ("%s: ioctl TCSANOW failed", argv[0]);
 }

 /*
 * the default is to catch SIGHUP, but if iflag is set ignore SIGHUP.
 */

 if (!iflag)
 signal (SIGHUP, slsignal);
 else
 sigignore (SIGHUP);

 /*
 * link slip to device, open ip, link ip with fd_dev
 */

 if ((fd_slip = open (slipname, O_RDWR)) < 0)
 slerror ("%s: open %s failed", argv[0], slipname);

 if ((fd_ip = open (ipname, O_RDWR)) < 0)
 slerror ("%s: open %s failed", argv[0], ipname);

 if (ioctl (fd_slip, I_LINK, fd_dev) < 0)
 slerror ("%s: ioctl I_LINK %s failed", argv[0], slipname);

 if ((fd_link = ioctl (fd_ip, I_LINK, fd_slip)) < 0)
 slerror ("%s: ioctl I_LINK %s failed", argv[0], ipname);

 /*
 * send a SIOCSIFNAME (set interface name) ioctl down the stream
 * referenced by fd_ip for the link associated with link identier
 * fd_link specifying the name ifname
 */

 strcpy (ifr.ifr_name,ifname);
 ifr.ifr_metric = fd_link;

 iocb.ic_cmd = SIOCSIFNAME;
 iocb.ic_timout = 15;
 iocb.ic_len = sizeof (ifr);
 iocb.ic_dp = (char *) &ifr;

 if (ioctl (fd_ip, I_STR, &iocb) < 0)
 slerror ("%s: ioctl SIOCSIFNAME (set interface name) failed", argv[0]);

 kill (getppid(), SIGINT); /* interrupt signal parent */
 pause (); /* wait forever */
}

/*
 * slsignal
 */

void slsignal (int x)
{
 fprintf (stderr,"%s: %s got a SIGHUP, ... exiting ...\n",program,ifname);
 if (ppid)
 kill (ppid, SIGHUP);
 exit (0);
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
 * usage ()
 */

usage ()
{
 printf ("\nUsage: %s [-i] system_name interface_name\n",program);
 printf ( "OR: %s -d [-i] device_name interface_name\n",program);
 printf ( "OR: %s - interface_name [process_id]\n",program);
 exit (1);
}
