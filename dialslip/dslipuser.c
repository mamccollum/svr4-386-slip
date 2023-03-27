/*
 * dslipuser.c
 *
 * Displays information on all of the users currently attached to the network.
 *
 * Copyright 1987 by University of California, Davis
 *
 * Greg Whitehead 10-1-87
 * Computing Services
 * University of California, Davis
 *
 * Revised: Geoff Arnold
 * Sun Microsystems Inc.
 * 10-28-87
 */

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <pwd.h>
#include "slip.h"

#ifdef USG
# include <sys/fcntl.h>
#endif

main(argc,argv)
int argc;
char **argv;
{
 int ufd;
 struct sl_urec urec;
 int free;
 struct passwd *upass;
 struct hostent *hh;
 int n = 0;
 int f = 0;

 /*
 * Open USER_FL.
 *
 */
 if ((ufd=open(USER_FL,O_RDONLY))<0) {
 perror(USER_FL);
 exit(1);
 }


 /*
 * Display USER_FL.
 *
 */
 while (read(ufd,&urec,sizeof(urec))==sizeof(urec)) {
 if (urec.sl_uid >=0) {
 n++;
 upass=getpwuid(urec.sl_uid);
 hh = gethostbyaddr(&urec.sl_haddr, 4, AF_INET);
 printf("User %s connected as %s (%s) via %s%d\n",
 upass->pw_name, hh->h_name, inet_ntoa(urec.sl_haddr),
 IF_NAME,urec.sl_unit);
 }
 else
 f++;
 }

 if (n == 0)
 printf("No dialup SLIP users connected.\n");

 printf("(%d free line%s)\n",f,(f==1)?"":"s");

 close(ufd);
}
