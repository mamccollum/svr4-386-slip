
/*
 * slip.c
 *
 * Dialup slip line allocator.
 *
 * Copyright 1987 by University of California, Davis
 *
 * Greg Whitehead 10-1-87
 * Computing Services
 * University of California, Davis
 */

/*
 * This program provides a means to obtain a slip line on a dialup tty.
 * It reads the file USER_FL to determine if an interface is available,
 * and to make sure that no internet address logs in more than once.
 * It updates the file with each login/disconnect in order to maintain
 * a record of which addresses are attached to which interfaces.
 *
 * In order to "ifconfig" and "slattach", slip must run setuid to root.
 *
 * Extensively modified by
 *
 * Geoff Arnold
 * Sun Microsystems Inc.
 * 10-28-87
 *
 * Modifications include:
 *
 * - allowing hostnames instead of dotted addresses (see also "mkslipuser")
 * - writing a line describing the configuration assigned by "sunslip"
 * to stdout; a corresponding PC program can parse this and use the
 * addressing information to check/update its local state
 * - for Sun systems, replacing the calls to slattach/ifconfig with the
 * equivalent ioctls
 *
 * Lightly modified by:
 * Sudji Husodo 2-8-91
 *
 * Modifications:
 * - ported to Unix System V/386 Release 4.0. (actually done by Alan Batie).
 * - modified to log slip activities to /var/slip/slip.log if the file exists.
 * - changed the call to "system" to fork and exec, so we don't have to setuid
 * slattach and ifconfig to root.
 */

#ifdef sun
#define YELLOW_PAGES 1 /* assumes SLIP server is a YP server too */
#endif sun

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <sgtty.h>
#include <time.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "slip.h"

#ifdef USG
# include <unistd.h>
# include <sys/fcntl.h>
#endif

char *ttyname();
char *getlogin();

void bye(); /* SIGHUP handler to remove USER_FL entry */


int ufd = -1; /* global info on USER_FL entry */
struct sl_urec urec;
int urec_i = -1;

char *tty, *name;

#ifdef sun
struct ifreq ifr;
#endif sun

char string [128];
char if_str [8];
char source[32];
char remote[32];
char pidstr[16];
int log = 1;
int fd_log;
time_t clock_val;

main(argc,argv)
int argc;
char **argv;
{
 int uid;

 FILE *hfd;
 char host_line[80], host_name[80], host_addr[80];
 struct in_addr addr;
 struct hostent *h, *hh;
 struct sl_urec urec_tmp;
 char sys_str[160];
 struct sgttyb sgtty;
 int disc = DISC;
 int unit;
 int x;
#ifdef sun
 int sockfd;
#endif sun

#ifdef sun
fprintf(stderr,"SUNSLIP\n");
#endif sun

 /*
 * if we can not open nor append the log file, turn log off
 */
 if ((fd_log = open (LOG_FL, O_RDWR | O_APPEND)) < 0)
 log = 0;
 else {
 clock_val = time(0);
 sprintf (string, "%s started by %s at %s", argv[0], getlogin(), ctime(&clock_val));
 write (fd_log, string, strlen(string));
 }

 /*
 * Mask all signals except SIGSTOP, SIGKILL, and SIGHUP.
 *
 */
 sigsetmask(~sigmask(SIGHUP));
 signal(SIGHUP,SIG_DFL);


 /*
 * Must have a tty to attach to.
 *
 */
 if ((tty=ttyname(0))==NULL) {
 fail("Bad login port", NULL);
 /* NOTREACHED */
 }


 /*
 * Need uid to put in USER_FL.
 * (checking it is paranoid, but complete)
 *
 */
 if ((uid=getuid())<0) {
 fail("Bad uid", NULL);
 /* NOTREACHED */
 }


 /*
 * Must have a login name to look up.
 *
 */
 if ((name=getlogin())==NULL) {
 fail("Bad login name", NULL);
 /* NOTREACHED */
 }

 /*
 * Open HOST_FL.
 *
 */
 if ((hfd=fopen(HOST_FL,"r"))==NULL) {
 fail("Can't open list of valid user-host mappings", NULL);
 /* NOTREACHED */
 }


 /*
 * look up login name in host file.
 *
 */
 for (;;) {
 if (fgets(host_line,80,hfd)==NULL) {
 fail("User %s is not authorized to connect to SLIP", name);
 /* NOTREACHED */
 }
 if (*host_line!='#' &&
 sscanf(host_line,"%s %s",host_addr,host_name)==2) {

 if (strncmp(name,host_name,8)==0) {
 break;
 }
 }
 }
 fclose(hfd);


 /*
 * Build internet addr from HOST_FL entry.
 *
 */
 if((h = gethostbyname(host_addr)) != NULL)
 addr.s_addr = *((int *)h->h_addr);
 else if ((addr.s_addr=inet_addr(host_addr))<0) {
 fail("Invalid address %s in hosts file", host_addr);
 /* NOTREACHED */
 }

 /*
 * Open USER_FL and get an exclusive lock on it.
 *
 */
 if ((ufd=open(USER_FL,O_RDWR))<0) {
 fail("Can't open SLIP user file", NULL);
 /* NOTREACHED */
 }
#ifdef USG
 /* This is a blocking lock; don't waste time... */
 if (lockf(ufd,F_LOCK,0L)<0) {
 close(ufd);
 fail("Unable to lock SLIP user file", NULL);
 /* NOTREACHED */
 }
#else
 if (flock(ufd,LOCK_EX)<0) {
 close(ufd);
 fail("Unable to lock SLIP user file", NULL);
 /* NOTREACHED */
 }
#endif


 /*
 * Make sure that this internet address isn't already logged in,
 * and look for a free interface
 *
 */
 for (x=0;read(ufd,&urec_tmp,sizeof(urec_tmp))==sizeof(urec_tmp);x++)
 if (urec_tmp.sl_uid<0) {
 if (urec_i<0) {
 urec_i=x;
 bcopy(&urec_tmp,&urec,sizeof(urec));
 }
 }
 else if (urec_tmp.sl_haddr.s_addr==addr.s_addr) {
 unlock_fail("Host %s is already attached",inet_ntoa(addr));
 /* NOTREACHED */
 }


 /*
 * If there is a free interface then take it.
 *
 */
 if (urec_i<0) {
 unlock_fail("All lines are busy. Try again later.");
 /* NOTREACHED */
 }

 h = gethostbyaddr(&addr, 4, AF_INET);
 printf("Attaching %s (%s)", h->h_name, inet_ntoa(addr));
 hh = gethostbyaddr(&urec.sl_saddr, 4, AF_INET);
#ifdef YELLOW_PAGES
 getdomainname(host_line, 79);
 printf(" to domain %s via %s (%s)\n",
 host_line, hh->h_name, inet_ntoa(urec.sl_saddr));
#else
 printf(" to network via %s (%s)\n",
 hh->h_name, inet_ntoa(urec.sl_saddr));
#endif

 if (ioctl(0, TIOCGETP, &sgtty) < 0) {
 perror("ioctl TIOCGETP");
 unlock_close(1);
 }
 sgtty.sg_flags = RAW | ANYP;
 if (ioctl(0, TIOCSETP, &sgtty) < 0) {
 perror("ioctl TIOCSETP");
 unlock_close(1);
 }
#ifndef USG
 if (ioctl(0, TIOCSETD, &disc) < 0) {
 perror("ioctl TIOCSETD");
 unlock_close(1);
 }
#endif


 /*
 * Retreive the SL unit number.
 *
 */
#ifndef sun
# ifdef USG
 unit = urec_i;
# else
 if (ioctl(0,TIOCGETU,&unit)<0) {
 perror("ioctl TIOCGETU");
 unlock_close(1);
 }
# endif
#else sun
 if (ioctl(0,TIOCGETD,&unit)<0) {
 perror("ioctl TIOCGETD");
 unlock_close(1);
 }
#endif sun

 /*
 * Build and write USER_FL entry.
 *
 */
 urec.sl_unit = unit;
 urec.sl_haddr.s_addr = addr.s_addr;
 urec.sl_uid = uid;
 if (lseek(ufd,urec_i*sizeof(urec),L_SET)<0) {
 sprintf(string,"%s: can't seek\n",USER_FL);
 fprintf(stderr,string);
 if (log)
 write (fd_log, string, strlen(string));
 unlock_close(1);
 }
 signal(SIGHUP,bye);
 if (write(ufd,&urec,sizeof(urec))!=sizeof(urec)) {
 sprintf(string,"%s: can't write\n",USER_FL);
 fprintf(stderr,string);
 if (log)
 write (fd_log, string, strlen(string));
 clean_user(1);
 }

 /*
 * Through with critical code. Unlock USER_FL.
 */
#ifdef USG
 lseek(ufd,0L,SEEK_SET);
 if (lockf(ufd,F_ULOCK,0L)<0) {
#else
 if (flock(ufd,LOCK_UN)<0) {
#endif
 sprintf(string,"%s: unlock failed\n", USER_FL);
 fprintf(stderr,string);
 if (log)
 write (fd_log, string, strlen(string));
 clean_user(1);
 }

#ifndef sun
# ifdef USG
 /*
 * slattach the line.
 */
 itoa (getpid(), pidstr);
 sprintf(sys_str,"%s - %s%u %s", SLATTACH, IF_NAME, urec.sl_unit, pidstr);
 sprintf(if_str,"%s%u", IF_NAME, urec.sl_unit);

 if (log) {
 sprintf (string, " %s: executing %s\n", name, sys_str);
 write (fd_log, string, strlen(string));
 }

 if (fork ())
 /* wait for child to finish */
 wait (0);
 else {
 /* execl doesn't return unless there's an error */
 execl (SLATTACH, SLATTACH, "-", if_str, pidstr, 0);
 printf("execl (%s): failed\n", sys_str);
 clean_user(1);
 }

# endif
 /*
 * ifconfig the slip line up.
 */
 sprintf(sys_str,"%s %s%u inet %s ",
 IFCONFIG,IF_NAME,urec.sl_unit,inet_ntoa(urec.sl_saddr));
 strcat(sys_str,inet_ntoa(urec.sl_haddr));
 strcat(sys_str,IFARGS);

 {
 FILE *xyzzy;
 xyzzy = fopen("/tmp/slip.log", "w");
 fputs(sys_str, xyzzy);
 fputc('\n', xyzzy);
 fclose(xyzzy);
 }

 if (log) {
 sprintf (string, " %s: executing %s\n", name, sys_str);
 write (fd_log, string, strlen(string));
 }

 if (fork ())
 /* wait for child to finish */
 wait (0);
 else {
 /* execl doesn't return unless there's an error */
 strcpy(source,inet_ntoa(urec.sl_saddr));
 strcpy(remote,inet_ntoa(urec.sl_haddr));
 execl (IFCONFIG, IFCONFIG, if_str, "inet", source, remote, "up", 0);
 printf ("execl (%s): failed\n", sys_str);
 clean_user(1);
 }
#else sun
 /*
 * Now instead of calling ifconfig (which in the Sun 3
 * version doesn't grok all of the 4.3BSD weirdness) we
 * revert to the old SIOCIFDSTADDR/SIOCSIFADDR stuff
 */
 sockfd = socket(AF_INET, SOCK_DGRAM, 0);
 if(sockfd < 0) {
 perror("sunslip: socket");
 clean_user(1);
 }
 sprintf(ifr.ifr_name, "sl%d", unit);
 fprintf(stderr, "sl%d\n", unit);
 getaddr(&urec.sl_haddr, (struct sockaddr_in *)&ifr.ifr_dstaddr);
 if(ioctl(sockfd, SIOCSIFDSTADDR, (caddr_t)&ifr) < 0) {
 perror("ioctl SIOCSIFDSTADDR");
 clean_user(1);
 }
 getaddr(&urec.sl_saddr, (struct sockaddr_in *)&ifr.ifr_addr);
 if(ioctl(sockfd, SIOCSIFADDR, (caddr_t)&ifr) < 0) {
 perror("ioctl SIOCSIFADDR");
 clean_user(1);
 }
#endif sun

 if (log) {
 sprintf (string, " %s: waiting for hangup\n",name);
 write (fd_log, string, strlen(string));
 }

 /*
 * Wait until carrier drops.
 * clean_usr() will be called.
 *
 */

 for(;;) {
 printf("pause returns %d\n", pause());
 }
}


unlock_close(status)
int status;
{
 /*
 * Unlock and close USER_FL, and exit with "status".
 *
 */

#ifdef USG
 lseek(ufd,0L,SEEK_SET);
 lockf(ufd,F_ULOCK,0L);
#else
 flock(ufd,LOCK_UN);
#endif
 close(ufd);
 exit(status);
}


unlock_fail(s1, s2)
char *s1;
char *s2;
{
 /*
 * Unlock and close USER_FL, and fail
 *
 */

#ifdef USG
 lseek(ufd,0L,SEEK_SET);
 lockf(ufd,F_ULOCK,0L);
#else
 flock(ufd,LOCK_UN);
#endif
 close(ufd);
 fail(s1, s2);
}


fail(s1, s2)
char *s1;
char *s2;
{
 fputs("\nConnection failure: ", stderr);
 fprintf(stderr, s1, s2);
 fputs("\n\n", stderr);

 if (log) {
 sprintf (string,"Connection failure: %s %s\n", s1, s2);
 write (fd_log, string, strlen(string));
 }

 exit(1);
}


clean_user(status)
int status;
{
 /*
 * mark line free in USER_FL, unlock and close USER_FL, and
 * exit with "status".
 *
 */

 urec.sl_uid = -1;
 if (lseek(ufd,urec_i*sizeof(urec),L_SET)<0 || write(ufd,&urec,sizeof(urec))!=sizeof(urec))
 status = -1;
 unlock_close(status);
}


void bye(int sig)
{
 /*
 * Handle SIGHUP.
 * Mark line free in USER_FL and exit with status 0.
 *
 */

printf("signal %d\n", sig);

 if (log) {
 clock_val = time(0);
 sprintf (string, " %s: caught signal %d, exiting ... %s", name, sig, ctime (&clock_val));
 write (fd_log, string, strlen(string));
 }
 clean_user(0);
}


#ifdef sun
getaddr(s, sin)
struct in_addr *s;
struct sockaddr_in *sin;
{
 sin->sin_family = AF_INET;
 sin->sin_addr = *s;
}
#endif sun



itoa (int n, char s[])
{
 int i, j;
 int c = 0;

 do {
 s[c++] = n % 10 + '0';
 } while ((n/=10) > 0);
 s[c] = '\0';

 for (i=0, j=c-1; i<j; i++, j--) {
 c = s[i];
 s[i] = s[j];
 s[j] = c;
 }
}
