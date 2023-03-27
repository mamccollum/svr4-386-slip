/*
 * slip.h
 *
 * Definitions for the dialup slip system.
 *
 * Copyright 1987 by University of California, Davis
 *
 * Greg Whitehead 10-1-87
 * Computing Services
 * University of California, Davis
 */

#define USER_FL "/etc/slip.user"
#define HOST_FL "/etc/slip.hosts"
#define CONFIG_FL "/etc/slip.config"
#define LOG_FL "/var/slip/slip.log"

#define SLATTACH "/usr/sbin/slattach"
#define IFCONFIG "/usr/sbin/ifconfig"
#define IFARGS " up"

#define DISC SLIPDISC
#define IF_NAME "sl"


struct sl_urec {
 int sl_uid; /* uid of logged in host (-1 if free) */
 int sl_unit; /* unit number for this login */
 struct in_addr sl_haddr; /* internet address of logged in host */
 struct in_addr sl_saddr; /* internet address of server side */
};
