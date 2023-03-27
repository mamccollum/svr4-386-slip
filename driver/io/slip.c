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
 * Serial Line Internet Protocol (SLIP) streams multiplexor driver for
 * Intel Unix System V/386 Release 4.0.
 *
 * The upper streams is supposed to be linked to the ip driver (/dev/ip)
 * and optionally linked to the slip hangup daemon. The lower streams can
 * linked to any number of serial driver.
 *
 * The slattach command builds the ip to slip to serial device links and
 * The slhangupd command is a daemon for receiving M_HANGUP message from
 * the serial driver.
 *
 * The packet framing protocol code, in the upper write and the lower
 * read service routine (slip_uwsrv and slip_lrsrv) is based from
 * tty_slip.c written by Rayan Zachariassen <rayan@ai.toronto.edu> and
 * Doug Kingston <dpk@morgan.com>.
 *
 * Author:
 * Sudji Husodo <sudji@indo.intel.com> 1/9/91
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>
#include <sys/syslog.h>
#include <sys/strlog.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/log.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/ddi.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/slip.h>

#define DL_PRIM_SIZE sizeof (union DL_primitives)

int slip_devflag = 0; /* V4.0 style driver */

#ifndef DEBUG
static
#endif
int slip_open(), slip_close(), slip_uwput(), slip_uwsrv(),
 slip_lwput(), slip_lwsrv(), slip_lrput(), slip_lrsrv();

static struct module_info minfo[5] = {
 SLIPM_ID, "slip", 0, 8192, 16384, 4096,
 SLIPM_ID, "slip", 0, 8192, 16384, 4096,
 SLIPM_ID, "slip", 0, 8192, 16384, 4096,
 SLIPM_ID, "slip", 0, 8192, 16384, 4096,
 SLIPM_ID, "slip", 0, 8192, 16384, 4096,
};

static struct qinit urinit = {
 NULL, NULL, slip_open, slip_close, NULL, &minfo[0], NULL };

static struct qinit uwinit = {
 slip_uwput, slip_uwsrv, slip_open, slip_close, NULL, &minfo[1], NULL };

static struct qinit lrinit = {
 slip_lrput, slip_lrsrv, slip_open, slip_close, NULL, &minfo[3], NULL };

static struct qinit lwinit = {
 slip_lwput, slip_lwsrv, slip_open, slip_close, NULL, &minfo[4], NULL };

struct streamtab slip_info = {
 &urinit, &uwinit, &lrinit, &lwinit };

slip_t *slip_hup = (slip_t *) 0;

extern struct ifstats *ifstats;
extern u_int slip_num;
extern slip_t slip_data[];

/*
 * slip_open
 */

#ifndef DEBUG
static
#endif
slip_open (q, devp, flag, sflag, credp)
queue_t *q;
dev_t *devp;
int flag;
int sflag;
struct cred *credp;
{
 register slip_t *p_slip;
 dev_t dev;
 int oldpri;
 mblk_t *bp;
 major_t major = getmajor (*devp);
 minor_t minor = getminor (*devp);

 STRLOG (SLIPM_ID,0,0,SL_TRACE,"slip_open: major %d minor %d",major,minor);

 /* find an unused entry in slip_data */

 if (sflag == CLONEOPEN) {
 for (dev=0, p_slip=&slip_data[0]; dev<slip_num; dev++, p_slip++)
 if (!p_slip->buf)
 break;
 minor = (minor_t) dev;
 }

 /* if there's no more free entry, return No Space error code */

 if (minor >= slip_num) {
 STRLOG (SLIPM_ID, 1, 0, SL_TRACE, "slip open: can't allocate device");
 return ENXIO;
 }

 /* initialized slip information */

 oldpri = splstr ();
 p_slip->state = DL_UNBOUND;
 p_slip->qtop = q;
 p_slip->qbot = NULL;
 p_slip->buf = (u_char *) kmem_alloc (SLIPMTU, KM_SLEEP);
 p_slip->qt_blocked = 0;
 drv_getparm (PPID, &p_slip->pid); /* keep process id */

 p_slip->escape = p_slip->overrun = p_slip->inlen = 0;
 p_slip->flags = IFF_UP | IFF_POINTOPOINT;
 p_slip->uname[0] = '\0';

 /* initialized interface and its statistics */

 p_slip->stats.ifs_name = (char *) p_slip->uname;
 p_slip->stats.ifs_unit = 0;
 p_slip->stats.ifs_active = 0;
 p_slip->stats.ifs_mtu = SLIPMTU;
 p_slip->stats.ifs_ipackets = p_slip->stats.ifs_opackets = 0;
 p_slip->stats.ifs_ierrors = p_slip->stats.ifs_oerrors = 0;
 p_slip->stats.ifs_collisions = 0;
 splx (oldpri);

 /* initialize read and write queue pointers to private data */

 q->q_ptr = (caddr_t) p_slip;
 WR(q)->q_ptr = (caddr_t) p_slip;

 /* set up the correct stream head flow control parameters */

 if (bp = allocb (sizeof (struct stroptions), BPRI_MED)) {
 struct stroptions *sop = (struct stroptions *) bp->b_rptr;
 bp->b_datap->db_type = M_SETOPTS;
 bp->b_wptr += sizeof (struct stroptions);
 sop->so_flags = SO_HIWAT | SO_LOWAT;
 sop->so_hiwat = minfo [2].mi_hiwat;
 sop->so_lowat = minfo [2].mi_lowat;
 putnext (q, bp);
 }
 *devp = makedevice (major, minor);
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip open: device %d coming up", minor);
 return (0);
}

/*
 * slip_close ()
 */

#ifndef DEBUG
static
#endif
slip_close (q)
queue_t *q;
{
 slip_t *p_slip;
 dev_t dev;
 int oldpri;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_close: going down ...");
 p_slip = (slip_t *) q->q_ptr;
 oldpri = splstr ();

 if (p_slip->buf)
 kmem_free (p_slip->buf, SLIPMTU);

 p_slip->state = DL_UNATTACHED;
 p_slip->buf = 0;
 p_slip->inlen = 0;
 p_slip->qtop = 0;
 p_slip->pid = 0;

 ifstats = p_slip->stats.ifs_next; /* reset ifstats pointers */
 splx (oldpri);
}

/*
 * slip_ioctl ()
 */

#ifndef DEBUG
static
#endif
slip_ioctl (q, mp)
queue_t *q;
mblk_t *mp;
{
 struct iocblk *iocp;
 struct ifreq *ifr;
 slip_t *p_slip;
 int oldpri;
 struct linkblk *lp;
 slip_t *pp;
 u_char *p;
 int n;

 p_slip = (slip_t *) q->q_ptr;
 iocp = (struct iocblk *) mp->b_rptr;

 STRLOG (SLIPM_ID, 1, 0, SL_TRACE, "slip_ioctl: enter: case ('%c',%d)", 0x00FF&(iocp->ioc_cmd>>8), 0x00FF&iocp->ioc_cmd);
 oldpri = splstr ();

 switch (iocp->ioc_cmd) {

 case REG_SLHUP:
 STRLOG (SLIPM_ID, 1, 0, SL_TRACE, "slip_ioctl: REG_SLHUP");
 if (slip_hup) {
 splx (oldpri);
 mp->b_datap->db_type = M_IOCNAK;
 qreply (q, mp);
 return (0);
 }
 else
 slip_hup = p_slip;
 break;

 case UNREG_SLHUP:
 STRLOG (SLIPM_ID, 1, 0, SL_TRACE, "slip_ioctl: UNREG_SLHUP");
 slip_hup = 0;
 break;

 case I_LINK:
 iocp->ioc_error = iocp->ioc_rval = iocp->ioc_count = 0;

 lp = (struct linkblk *) mp->b_cont->b_rptr;
 p_slip->qbot = lp->l_qbot;
 p_slip->qbot->q_ptr = (char *) p_slip;
 OTHERQ (p_slip->qbot)->q_ptr = (char *) p_slip;
 break;

 case I_UNLINK:
 iocp->ioc_error = iocp->ioc_rval = iocp->ioc_count = 0;
 p_slip->qbot = NULL;
 break;

 case SIOCSIFNAME:
 ifr = (struct ifreq *) mp->b_cont->b_rptr;

 /* copy interface name to local slip structure */

 /*
 * interface name (ifr->ifr_name) contains the name and unit, e.g.
 * "sl0", "sl12", "emd0", "wd1", etc. Store the name in slip->uname
 * and unit in slip->unit. If unit is not supplied, e.g. "slip",
 * then unit number is assumed to be zero.
 */

 strncpy (p_slip->uname, ifr->ifr_name, IFNAMSIZ); /* copy name */
 n = 0;

 /* starting from the last char, find the first non-digit char */

 p = p_slip->uname + strlen(p_slip->uname) - 1;
 while ('0' <= *p && *p <= '9')
 p--;

 /* calculate integer, replace them with nulls */

 while (*++p) {
 n = 10*n + (*p-'0');
 *p = '\0';
 }
 p_slip->stats.ifs_unit = n; /* set ifs unit number */

 /* search for matching interface name and unit */

 for (n=0, pp=&slip_data[0]; n<slip_num; n++, pp++)
 if (pp != p_slip && pp->buf && !strcmp (pp->uname,p_slip->uname) &&
 pp->stats.ifs_unit == p_slip->stats.ifs_unit)
 break;

 if (n < slip_num) { /* found matching ifname */
 splx (oldpri);
 mp->b_datap->db_type = M_IOCNAK; /* Negative Ack reply */
 qreply (q, mp);
 return (0);
 }
 else { /* ifname is unique */
 p_slip->stats.ifs_next = ifstats; /* set ifstats pointers */
 ifstats = &p_slip->stats; /* used for statistics */
 } /* by netstat command */
 break;

 case SIOCGIFFLAGS:
 ((struct iocblk_in *)iocp)->ioc_ifflags = p_slip->flags;
 break;

 case SIOCSIFFLAGS:
 p_slip->flags = ((struct iocblk_in *)iocp)->ioc_ifflags;
 break;

 case SIOCSIFADDR:
 p_slip->flags |= IFF_RUNNING;
 ((struct iocblk_in *)iocp)->ioc_ifflags |= IFF_RUNNING;
 break;

 default:
 break;
 }
 splx (oldpri);
 mp->b_datap->db_type = M_IOCACK;
 qreply (q, mp);
}

/*
 * slip_uwput ()
 */

#ifndef DEBUG
static
#endif
slip_uwput (q, mp)
queue_t *q;
mblk_t *mp;
{
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_uwput: case 0x%2.2x", mp->b_datap->db_type);

 switch (mp->b_datap->db_type) {

 case M_FLUSH:
 if (*mp->b_rptr & FLUSHW) {
 flushq(q, FLUSHALL);
 *mp->b_rptr &= ~FLUSHW;
 }
 if (*mp->b_rptr & FLUSHR)
 qreply(q, mp);
 else
 freemsg(mp);
 break;

 case M_IOCTL:
 slip_ioctl (q, mp);
 break;

 case M_PROTO:
 case M_PCPROTO:
 slip_dl_cmds (q, mp);
 break;

 default:
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_uwput: unknown message type, passing message to the next queue");
 putnext (((slip_t *)q->q_ptr)->qbot, mp);
 break;
 }
}

/*
 * slip_uwsrv ()
 */

#ifndef DEBUG
static
#endif
slip_uwsrv (q)
register queue_t *q;
{
 register mblk_t *mp, *mpd, *mp2;
 register u_char *cp;
 register int pktlen, num;
 register slip_t *p_slip;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_uwsrv: enter");

 while ((mp = getq(q)) != NULL) {
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_uwsrv: got message from q");
 p_slip = (slip_t *) q->q_ptr;


 if (!canput (p_slip->qbot)) {
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_uwsrv: can't put message to qbot");
 putbq (q, mp);
 p_slip->qt_blocked = 1;
 return;
 }
 pktlen = 0;

 /*
 * count the number of special characters (END & ESC)
 */

 num = 2; /* END char is put at the start and end of packet */

 for (mpd = mp->b_cont; mpd != 0; mpd = mpd->b_cont) {
 pktlen += (mpd->b_wptr - mpd->b_rptr);

 for (cp = mpd->b_rptr; cp < mpd->b_wptr; cp++) {
 if (*cp == END || *cp == ESC)
 num++;
 }
 }
 STRLOG (SLIPM_ID, 1, 0, SL_TRACE, "slip_uwsrv: # of bytes in packet = %d; # of special character in packet: %d", pktlen, num);

 /*
 * allocate message block to be sent down stream
 */

 if ((mp2 = allocb (pktlen + num, BPRI_MED)) == NULL) {
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_uwsrv: can't allocate message block - dropping outgoing message");
 p_slip->stats.ifs_oerrors++;
 freemsg (mp);
 return;
 }
 /*
 * frame packet, escape special characters ESC and END
 */

 *mp2->b_wptr++ = END;

 for (mpd = mp->b_cont; mpd != 0; mpd = mpd->b_cont) {
 for (cp = mpd->b_rptr; cp < mpd->b_wptr; cp++) {
 if (*cp == END) {
 *mp2->b_wptr++ = ESC;
 *mp2->b_wptr++ = ESC_END;
 }
 else if (*cp == ESC) {
 *mp2->b_wptr++ = ESC;
 *mp2->b_wptr++ = ESC_ESC;
 }
 else
 *mp2->b_wptr++ = *cp;
 }
 }
 *mp2->b_wptr++ = END;

 mp2->b_datap->db_type = M_DATA;
 p_slip->stats.ifs_opackets++;
 freemsg (mp);
 slip_lwput (p_slip->qbot, mp2);
 }
}

/*
 * slip_lwput
 */

#ifndef DEBUG
static
#endif
slip_lwput (q, mp)
queue_t *q;
mblk_t *mp;
{
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_lwput: enter %x, %x",q , mp);

 if (canput(q->q_next)) {
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_lwput: putnext");
 putnext(q, mp);
 }
 else {
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_lwput: putq");
 putq(q, mp);
 }
}

/*
 * slip_lwsrv (q)
 */

#ifndef DEBUG
static
#endif
slip_lwsrv (q)
queue_t *q;
{
 mblk_t *mp;
 slip_t *p_slip;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE,"slip_lwsrv: enter");

 while (mp = getq(q)) {
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE,"slip_lwsrv: getq (%x) = %x",q, mp);
 if (canput(q->q_next)) {
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE,"slip_lwsrv: putnext");
 putnext(q, mp);
 }
 else {
 STRLOG (SLIPM_ID, 0, 0, SL_TRACE,"slip_lwsrv: putbq");
 putbq(q, mp);
 return;
 }
 }
 p_slip = (slip_t *) q->q_ptr;

 if (p_slip->qt_blocked) {
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_lwsrv: slip_uwsrv is blocked, enable it");
 /*
 * qtop is the upper read q and
 * we want to enable the upper write q
 */
 qenable (WR(p_slip->qtop));
 p_slip->qt_blocked = 0;
 }
}

/*
 * slip_lrput ()
 */

#ifndef DEBUG
static
#endif
slip_lrput (q, mp)
queue_t *q;
mblk_t *mp;
{
 slip_t *p_slip;
 mblk_t *resp;
 int device;
 int oldpri;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_lrput: enter: case 0x%2.2x", mp->b_datap->db_type);
 p_slip = (slip_t *) q->q_ptr;

 switch (mp->b_datap->db_type) {

 case M_DATA:
 STRLOG (SLIPM_ID,2,0,SL_TRACE,"slip_lrput: putq (%x, %x)", q, mp);
 putq (q, mp);
 break;

 case M_FLUSH:
 STRLOG (SLIPM_ID,2,0,SL_TRACE,"slip_lrput: M_FLUSH type = %x",*mp->b_rptr);
 if (*mp->b_rptr & FLUSHR)
 flushq(q, FLUSHALL);
 if (*mp->b_rptr & FLUSHW) {
 *mp->b_rptr &= ~FLUSHR;
 flushq(WR(q), FLUSHALL);
 qreply(q, mp);
 } else
 freemsg(mp);
 return;

 case M_HANGUP:
 oldpri = splstr ();
 /*
 * if pid is set, ignore message
 */
 if (p_slip->pid == 0)
 freemsg (mp);
 /*
 * else if slip hangup daemon exists send pid to the daemon
 */
 else if (slip_hup && (resp = allocb (sizeof(pid_t),BPRI_MED))) {
 STRLOG (SLIPM_ID,1,0,SL_TRACE,
 "slip_lrput: sending pid %d to hangup daemon", p_slip->pid);
 *(pid_t *) resp->b_wptr = p_slip->pid;
 resp->b_wptr += sizeof (pid_t);
 resp->b_datap->db_type = M_PCPROTO;
 putnext (slip_hup->qtop, resp);
 freemsg (mp);
 }
 /*
 * else pass the message upstream
 */
 else
 putnext (p_slip->qtop, mp);

 splx (oldpri);
 break;

 default:
 putnext (p_slip->qtop, mp);
 break;
 }
}

/*
 * slip_lrsrv ()
 */

#ifndef DEBUG
static
#endif
slip_lrsrv (q)
queue_t *q;
{
 register u_char *cp;
 register slip_t *p_slip;
 mblk_t *bp, *mp, *mp1, *mp2;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_lrsrv: enter");

 while ((mp = getq(q)) != NULL) {
 p_slip = (slip_t *) q->q_ptr;

STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_lrsrv: q=%x, mp=%x;", q, mp);
 if (!canput (p_slip->qtop->q_next)) {
 putbq (q, mp);
 return;
 }
 p_slip = (slip_t *) q->q_ptr;

 for (bp = mp; bp != 0; bp = bp->b_cont) {
 for (cp = bp->b_rptr; cp < bp->b_wptr; cp++) {
 if (*cp == END) {
 if (p_slip->inlen < sizeof (struct ip))
 ; /* ignore packet */

 else if (p_slip->overrun)
 p_slip->stats.ifs_ierrors++;

 else if ((mp1=allocb (DL_UNITDATA_IND_SIZE,BPRI_MED))==NULL)
 p_slip->stats.ifs_ierrors++;

 else if ((mp2 = allocb (p_slip->inlen, BPRI_MED)) == NULL) {
 p_slip->stats.ifs_ierrors++;
 freemsg (mp1);
 }
 else {
 p_slip->stats.ifs_ipackets++; /* send unit data */
 linkb (mp1, mp2); /* indication up */
 slip_dl_unitdata_ind (q, mp1); /* stream */
 putnext (p_slip->qtop, mp1);
 }
 p_slip->inlen = 0; /* reset info for */
 p_slip->overrun = 0; /* receiving data */
 p_slip->escape = 0;
 }
 else if (p_slip->inlen >= SLIPMTU)
 p_slip->overrun = 1;

 else if (*cp == ESC) /* if data is ESC */
 p_slip->escape = 1;

 else if (p_slip->escape) {
 p_slip->escape = 0;

 if (*cp == ESC_END)
 *(p_slip->buf + p_slip->inlen++) = END;
 else if (*cp == ESC_ESC)
 *(p_slip->buf + p_slip->inlen++) = ESC;
 else
 *(p_slip->buf + p_slip->inlen++) = *cp;
 }
 else
 *(p_slip->buf + p_slip->inlen++) = *cp;
 }
 bp->b_rptr = cp;
 }
 freemsg (mp);
 }
}

/*
 * slip_dl_cmds ()
 */

#ifndef DEBUG
static
#endif
slip_dl_cmds (q, mp)
queue_t *q;
mblk_t *mp;
{
 union DL_primitives *p_dl;
 mblk_t *response;
 slip_t *p_slip;
 int oldpri;

 if ((response = allocb (DL_PRIM_SIZE, BPRI_MED)) == NULL) {
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_dl_cmd: can't allocate response buffer");
 freemsg (mp);
 return;
 }

 p_slip = (slip_t *) q->q_ptr;
 p_dl = (union DL_primitives *) mp->b_datap->db_base;

 switch (p_dl->dl_primitive) {

 case DL_INFO_REQ:
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_dl_cmd: DL_INFO_REQ");
 slip_dl_info_ack (q, mp, response);
 break;

 case DL_BIND_REQ:
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_dl_cmd: DL_BIND_REQ");
 if (p_slip->state == DL_UNBOUND) {
 oldpri = splstr ();
 p_slip->sap = ((dl_bind_req_t *)p_dl)->dl_sap;
 p_slip->state = DL_IDLE;
 p_slip->stats.ifs_active = 1;
 splx (oldpri);

 slip_dl_bind_ack (q, mp, response);
 }
 else
 slip_dl_error_ack (q, mp, response, DL_OUTSTATE);
 break;

 case DL_UNBIND_REQ:
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_dl_cmd: DL_UNBIND_REQ");
 if (p_slip->state == DL_IDLE) {
 oldpri = splstr ();
 p_slip->state = DL_UNBOUND;
 p_slip->stats.ifs_active = 0;
 splx (oldpri);

 flushq (q, FLUSHDATA); /* Flush both q's */
 flushq (RD(q), FLUSHDATA);

 slip_dl_ok_ack (q, mp, response);
 }
 else
 slip_dl_error_ack (q, mp, response, DL_OUTSTATE);

 break;

 case DL_UNITDATA_REQ:
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_dl_cmd: DL_UNITDATA_REQ");
 if (p_slip->state == DL_IDLE) {
 STRLOG (SLIPM_ID,2,0,SL_TRACE,"slip_dl_cmd: putq (%x, %x)", q, mp);
 putq (q, mp);
 return;
 }
 else
 slip_dl_error_ack (q, mp, response, DL_OUTSTATE);

 break;

 default:
 STRLOG (SLIPM_ID,1,0,SL_TRACE,"slip_dl_cmd: default 0x%2.2x",p_dl->dl_primitive);
 slip_dl_error_ack (q, mp, response, DL_UNSUPPORTED);
 break;
 }
 freemsg (mp);
 putnext (RD(q), response);
}

/*
 * slip_dl_info_ack ()
 */

#ifndef DEBUG
static
#endif
slip_dl_info_ack (q, mp, response)
queue_t *q;
mblk_t *mp;
mblk_t *response;
{
 dl_info_ack_t *p_info_ack;
 slip_t *p_slip;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_dl_info_ack: enter");

 p_slip = (slip_t *) q->q_ptr;

 p_info_ack = (dl_info_ack_t *) response->b_wptr;
 p_info_ack->dl_primitive = DL_INFO_ACK;
 p_info_ack->dl_max_sdu = SLIPMTU;

 p_info_ack->dl_min_sdu = 46; /* ????? */
 p_info_ack->dl_addr_length = 0; /* ????? MAC_ADD_SIZE*/
 p_info_ack->dl_mac_type = DL_CHAR; /* ????? */

 p_info_ack->dl_current_state = p_slip->state;
 p_info_ack->dl_service_mode = DL_CLDLS; /* connecionless DL */

 p_info_ack->dl_qos_length = 0; /* ???? */
 p_info_ack->dl_qos_offset = 0; /* ???? */
 p_info_ack->dl_qos_range_length = 0; /* ???? */
 p_info_ack->dl_qos_range_offset = 0; /* ???? */

 p_info_ack->dl_provider_style = DL_STYLE1;

 p_info_ack->dl_addr_offset = 0; /* ???? */
 p_info_ack->dl_growth = 0;

 response->b_datap->db_type = M_PCPROTO;
 response->b_wptr += DL_INFO_ACK_SIZE;
}

/*
 * slip_dl_bind_ack ()
 */

#ifndef DEBUG
static
#endif
slip_dl_bind_ack (q, mp, response)
queue_t *q;
mblk_t *mp;
mblk_t *response;
{
 dl_bind_req_t *p_dl;
 dl_bind_ack_t *p_bind;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_dl_bind_ack: enter");

 p_dl = (dl_bind_req_t *) mp->b_datap->db_base;

 p_bind = (dl_bind_ack_t *) response->b_wptr;
 p_bind->dl_primitive = DL_BIND_ACK;
 p_bind->dl_sap = p_dl->dl_sap;
 p_bind->dl_addr_length = 0;
 p_bind->dl_addr_offset = 0;

 response->b_wptr += DL_BIND_ACK_SIZE;
 response->b_datap->db_type = M_PCPROTO;
}

/*
 * slip_dl_ok_ack ()
 */

#ifndef DEBUG
static
#endif
slip_dl_ok_ack (q, mp, response)
queue_t *q;
mblk_t *mp;
mblk_t *response;
{
 union DL_primitives *p_dl;
 dl_ok_ack_t *p_ok_ack;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_dl_ok_ack: enter");

 p_dl = (union DL_primitives *) mp->b_datap->db_base;

 p_ok_ack = (dl_ok_ack_t *)(response->b_wptr);
 p_ok_ack->dl_primitive = DL_OK_ACK;
 p_ok_ack->dl_correct_primitive = p_dl->dl_primitive;

 response->b_wptr += DL_OK_ACK_SIZE;
 response->b_datap->db_type = M_PCPROTO;
}

/*
 * slip_dl_error_ack
 */

#ifndef DEBUG
static
#endif
slip_dl_error_ack (q, mp, response, dl_errno)
queue_t *q;
mblk_t *mp;
mblk_t *response;
ulong dl_errno;
{
 union DL_primitives *p_dl;
 dl_error_ack_t *p_error;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_dl_error_ack: enter");

 p_dl = (union DL_primitives *) mp->b_datap->db_base;

 p_error = (dl_error_ack_t *) response->b_wptr;
 p_error->dl_primitive = DL_ERROR_ACK;
 p_error->dl_error_primitive = p_dl->dl_primitive;
 p_error->dl_errno = dl_errno;
 p_error->dl_unix_errno = 0;

 response->b_wptr += DL_ERROR_ACK_SIZE;
 response->b_datap->db_type = M_PCPROTO;
}

/*
 * slip_dl_unitdata_ind ()
 */

#ifndef DEBUG
static
#endif
slip_dl_unitdata_ind (q, mp)
queue_t *q;
mblk_t *mp;
{
 dl_unitdata_ind_t *p_dl;
 slip_t *p_slip;

 STRLOG (SLIPM_ID, 0, 0, SL_TRACE, "slip_dl_unitdata_ind: enter");

 p_dl = (dl_unitdata_ind_t *) mp->b_wptr;
 p_dl->dl_primitive = DL_UNITDATA_IND;
 p_dl->dl_dest_addr_length = 0;
 p_dl->dl_dest_addr_offset = DL_UNITDATA_IND_SIZE;
 p_dl->dl_src_addr_length = 0;
 p_dl->dl_src_addr_offset = p_dl->dl_dest_addr_offset + p_dl->dl_dest_addr_length;

 mp->b_wptr += DL_UNITDATA_IND_SIZE;
 mp->b_datap->db_type = M_PROTO;

 /* copy packet received to the next message block */

 p_slip = (slip_t *) q->q_ptr;

 bcopy ((caddr_t)p_slip->buf, (caddr_t)mp->b_cont->b_wptr, p_slip->inlen);

 mp->b_cont->b_wptr += p_slip->inlen;
}
