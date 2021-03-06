/***************************************************************
 *
 * (C) 2011-13 Nicola Bonelli <nicola.bonelli@cnit.it>
 *             Andrea Di Pietro <andrea.dipietro@for.unipi.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 ****************************************************************/

#ifndef _PF_Q_H_
#define _PF_Q_H_

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/filter.h>

#define Q_VERSION               "2.0"

#define Q_MAX_CPU               64
#define Q_MAX_ID                64
#define Q_MAX_GROUP             64

#define Q_MAX_DEVICE            256
#define Q_MAX_DEVICE_MASK       (Q_MAX_DEVICE-1)
#define Q_MAX_HW_QUEUE          256
#define Q_MAX_HW_QUEUE_MASK     (Q_MAX_HW_QUEUE-1)

#else  /* user space */

#include <linux/filter.h>
#include <linux/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>

#endif /* __KERNEL__ */

/* Common header */

#define PF_Q    27          /* packet q domain */


struct pfq_hdr
{
    uint64_t data;          /* state from pfq_annotation */

    union
    {
        unsigned long long tv64;
        struct {
            uint32_t    sec;
            uint32_t    nsec;
        } tv;               /* note: struct timespec is badly defined for 64 bits arch. */
    } tstamp;

    int         if_index;   /* interface index */
    int         gid;        /* gruop id */

    uint16_t    len;        /* length of the packet (off wire) */
    uint16_t    caplen;     /* bytes captured */

    union
    {
        struct
        {
            uint16_t vlan_vid:12,   /* 8021q vlan id */
                     reserved:1,    /* 8021q reserved bit */
                     vlan_prio:3;   /* 8021q vlan priority */
        } vlan;

        uint16_t     vlan_tci;
    } un;

    uint8_t     hw_queue;   /* 256 queues per device */
    uint8_t     commit;

}; /* __attribute__((packed)); */


/*
    +-----------------+------------------+          +------------------+
    | pfq_queue_descr | pfq_hdr | packet | ...      | pfq_hdr | packet | ...
    +-----------------+------------------+          +------------------+
                      +                             +
                      | <------+ queue 1  +-------> |  <----+ queue 2 +-------->
                      +                             +
 */


struct pfq_queue_descr
{
    volatile unsigned int   data;
    volatile int            poll_wait;

} __attribute__((aligned(8)));


#define MPDB_QUEUE_SLOT_SIZE(x)    ALIGN(sizeof(struct pfq_hdr) + x, 8)
#define MPDB_QUEUE_INDEX(data)     (((data) & 0xff000000U) >> 24)
#define MPDB_QUEUE_LEN(data)       ((data) & 0x00ffffffU)

/* PFQ socket options */

#define Q_SO_TOGGLE_QUEUE           1       /* enable = 1, disable = 0 */
#define Q_SO_ADD_BINDING            2
#define Q_SO_REMOVE_BINDING         3
#define Q_SO_SET_TSTAMP             4
#define Q_SO_SET_CAPLEN             5
#define Q_SO_SET_SLOTS              6
#define Q_SO_SET_OFFSET             7
#define Q_SO_GROUP_JOIN             8
#define Q_SO_GROUP_LEAVE            9
#define Q_SO_GROUP_CONTEXT          10
#define Q_SO_GROUP_FUN              11
#define Q_SO_GROUP_RESET            12

#define Q_SO_GROUP_FPROG            13      /* Berkeley packet filter */
#define Q_SO_GROUP_VLAN_FILT_TOGGLE 14      /* enable/disable VLAN filters */
#define Q_SO_GROUP_VLAN_FILT        15      /* enable/disable VLAN ID filters */

#define Q_SO_GET_ID                 20
#define Q_SO_GET_STATUS             21      /* 1 = enabled, 0 = disabled */
#define Q_SO_GET_STATS              23
#define Q_SO_GET_TSTAMP             24
#define Q_SO_GET_QUEUE_MEM          25      /* size of the whole dbmp queue (bytes) */
#define Q_SO_GET_CAPLEN             26
#define Q_SO_GET_SLOTS              27
#define Q_SO_GET_OFFSET             28
#define Q_SO_GET_GROUPS             29
#define Q_SO_GET_GROUP_STATS        30
#define Q_SO_GET_GROUP_CONTEXT      31

/* general defines */

#define Q_ANY_DEVICE         -1
#define Q_ANY_QUEUE          -1
#define Q_ANY_GROUP          -1

/* timestamp */

#define Q_TSTAMP_OFF          0       /* default */
#define Q_TSTAMP_ON           1

/* vlan */

#define Q_VLAN_PRIO_MASK     0xe000
#define Q_VLAN_VID_MASK      0x0fff
#define Q_VLAN_TAG_PRESENT   0x1000

#define Q_VLAN_UNTAG         0
#define Q_VLAN_ANYTAG       -1


struct pfq_vlan_toggle
{
    int gid;
    int vid;
    int toggle;
};


/* struct used for binding */


struct pfq_binding
{
    int gid;
    int if_index;
    int hw_queue;
};

/* group policies */

#define Q_GROUP_UNDEFINED       0
#define Q_GROUP_PRIVATE         1
#define Q_GROUP_RESTRICTED      2
#define Q_GROUP_SHARED          3

/* class type */

#define Q_CLASS(n)              (1U<<n)
#define Q_CLASS_MAX             sizeof(unsigned int)<<3

#define Q_CLASS_DEFAULT         Q_CLASS(0)
#define Q_CLASS_DATA            Q_CLASS_DEFAULT
#define Q_CLASS_CONTROL         Q_CLASS(1)
#define Q_CLASS_ANY             -1


struct pfq_group_join
{
    int gid;
    int policy;
    unsigned int class_mask;
};


/* steering functions */

#define Q_FUN_NAME_LEN        64
#define Q_FUN_MAX             8     /* max fun binding: f1 >>= f2 >>= f3...*/

struct pfq_group_function
{
    const char *name;
    int gid;
    int level;
};


struct pfq_group_context
{
    void       * context;
    size_t       size;      // sizeof(context)
    int gid;
    int level;
};


/* pfq_fprog: per-group sock_fprog */

struct pfq_fprog
{
    int gid;
    struct sock_fprog fcode;
};


/* pfq statistics for socket and groups */

struct pfq_stats
{
    unsigned long int recv;   /* received by the queue         */
    unsigned long int lost;   /* queue is full, packet lost... */
    unsigned long int drop;   /* by filter                     */
};


#endif /* _PF_Q_H_ */
