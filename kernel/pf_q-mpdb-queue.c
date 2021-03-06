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

#include <pf_q-mpdb-queue.h>
#include <linux/pf_q-fun.h>

void * mpdb_queue_alloc(struct pfq_opt *pq, size_t queue_mem, size_t *tot_mem)
{
	/* calculate the size of the buffer */

	size_t tm = PAGE_ALIGN(queue_mem);

	/* align bufflen to page size */

	size_t num_pages = tm / PAGE_SIZE; void *addr;

	num_pages += (num_pages + (PAGE_SIZE-1)) & (PAGE_SIZE-1);
	*tot_mem = num_pages*PAGE_SIZE;

	/* Memory is already zeroed */

        addr = vmalloc_user(*tot_mem);
	if (addr == NULL)
	{
		printk(KERN_WARNING "[PFQ|%d] pfq_queue_alloc: out of memory!", pq->id);
		*tot_mem = 0;
		return NULL;
	}

	pr_devel("[PFQ|%d] queue caplen:%lu mem:%lu\n", pq->id, pq->caplen, *tot_mem);
	return addr;
}


void mpdb_queue_free(struct pfq_opt *pq)
{
	if (pq->addr) {
		pr_devel("[PFQ|%d] queue freed.\n", pq->id);
		vfree(pq->addr);

		pq->addr = NULL;
		pq->queue_mem = 0;
	}
}


static inline
void *pfq_memcpy(void *to, const void *from, size_t len)
{
	switch(len)
	{
		case 64 : return __builtin_memcpy(to, from, 64);
		case 128: return __builtin_memcpy(to, from, 128);
		case 256: return __builtin_memcpy(to, from, 256);
		case 512: return __builtin_memcpy(to, from, 512);
		default:  return memcpy(to, from, len);
	}
}


inline
char *mpdb_slot_ptr(struct pfq_opt *pq, struct pfq_queue_descr *qd, int index, int slot)
{
	return (char *)(qd+1) + ((index&1) * pq->slots + slot) * pq->slot_size;
}


size_t mpdb_enqueue_batch(struct pfq_opt *pq, unsigned long bitqueue, int burst_len, struct pfq_queue_skb *skbs, int gid)
{
	struct pfq_queue_descr *queue_descr = (struct pfq_queue_descr *)pq->addr;
	int data, q_len, q_index;
	struct sk_buff *skb;
	size_t sent = 0;
	unsigned int n;
	char *this_slot;

	data = atomic_read((atomic_t *)&queue_descr->data);

        if (unlikely(MPDB_QUEUE_LEN(data) > pq->slots))
		return 0;

	data = atomic_add_return(burst_len, (atomic_t *)&queue_descr->data);

	q_len     = MPDB_QUEUE_LEN(data) - burst_len;
	q_index   = MPDB_QUEUE_INDEX(data);
        this_slot = mpdb_slot_ptr(pq, queue_descr, q_index, q_len);

	queue_for_each_bitmask(skb, bitqueue, n, skbs)
	{
		unsigned int bytes = likely (skb->len > (int)pq->offset) ? min((int)skb->len - (int)pq->offset, (int)pq->caplen) : 0;

		size_t slot_index = q_len + sent;

		volatile struct pfq_hdr *hdr = (struct pfq_hdr *)this_slot;
		char                    *pkt = (char *)(hdr+1);

		struct timespec ts;

		if (unlikely(slot_index > pq->slots))
		{
			if ( queue_descr->poll_wait ) {
				wake_up_interruptible(&pq->waitqueue);
			}
			return sent;
		}

		/* copy bytes of packet */

		if (likely(bytes))
		{
			/* packets might still come from a regular sniffer */

			if (
#ifdef PFQ_USE_SKB_LINEARIZE
			   	unlikely(skb_is_nonlinear(skb))
#else
		           	skb_is_nonlinear(skb)
#endif
			   )
		      	{
				if (skb_copy_bits(skb, (int)pq->offset, pkt, bytes) != 0)
				{
					printk(KERN_WARNING "[PFQ] BUG! skb_copy_bits failed (bytes=%u, skb_len=%d mac_len=%d q_offset=%lu)!\n",
							    bytes, skb->len, skb->mac_len, pq->offset);
					return 0;
				}
			}
			else
			{
				pfq_memcpy(pkt, skb->data + pq->offset, bytes);
			}
		}

                /* copy state from pfq_annotation */

                hdr->data = pfq_skb_annotation(skb)->state;

		/* setup the header */

		if (pq->tstamp != 0)
		{
			skb_get_timestampns(skb, &ts);
			hdr->tstamp.tv.sec  = (uint32_t)ts.tv_sec;
			hdr->tstamp.tv.nsec = (uint32_t)ts.tv_nsec;
		}

		hdr->if_index    = skb->dev->ifindex & 0xff;
		hdr->gid         = gid;

		hdr->len         = (uint16_t)skb->len;
		hdr->caplen 	 = (uint16_t)bytes;
		hdr->un.vlan_tci = skb->vlan_tci & ~VLAN_TAG_PRESENT;
		hdr->hw_queue    = (uint8_t)(skb_get_rx_queue(skb) & 0xff);

		/* commit the slot (release semantic) */

		smp_wmb();

		hdr->commit = (uint8_t)q_index;

		if (unlikely((slot_index & 16383) == 0) &&
			     (slot_index >= (pq->slots >> 1)) &&
			     queue_descr->poll_wait)
		{
		        wake_up_interruptible(&pq->waitqueue);
		}

		sent++;

		this_slot += pq->slot_size;
	}

	return sent;
}

