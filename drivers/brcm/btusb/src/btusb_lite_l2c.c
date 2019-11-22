/*
 *
 * btusb_lite_l2c.c
 *
 *
 *
 * Copyright (C) 2011-2015 Broadcom Corporation.
 *
 *
 *
 * This software is licensed under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation (the "GPL"), and may
 * be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL for more details.
 *
 *
 * A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php
 * or by writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA
 *
 *
 */

#include "btusb.h"
#include <linux/jiffies.h>

/*
 * Definitions
 */

/*
 * Local functions
 */
static UINT8 *btusb_lite_l2c_write_header(UINT8 *p_data, UINT16 length, UINT16 cid);

/*******************************************************************************
**
** Function         btusb_lite_l2c_add
**
** Description      Synchronize (Add) L2CAP Stream
**
** Returns          Status.
**
*******************************************************************************/
int btusb_lite_l2c_add(struct btusb *p_dev, tL2C_STREAM_INFO *p_l2c_stream)
{
    int idx;
    struct btusb_lite_l2c_cb *p_l2c = &p_dev->lite_cb.s.l2c;
    struct btusb_lite_l2c_ccb *p_l2c_ccb;

    /* Check if this L2CAP Stream exists */
    for (idx = 0, p_l2c_ccb = p_l2c->ccb ; idx < ARRAY_SIZE(p_l2c->ccb) ; idx++, p_l2c_ccb++)
    {
        if ((p_l2c_ccb->in_use) &&
            (p_l2c_ccb->handle == p_l2c_stream->handle))
        {
            BTUSB_INFO("CCB=%d was already allocated. Update it.\n", idx);
            break;
        }
    }
    /* If Not found */
    if (idx == BTM_SYNC_INFO_NUM_STR)
    {
        /* Look for a free CCB */
        for (idx = 0, p_l2c_ccb = p_l2c->ccb ; idx < ARRAY_SIZE(p_l2c->ccb) ; idx++, p_l2c_ccb++)
        {
            if (p_l2c_ccb->in_use == FALSE)
            {
                BTUSB_INFO("CCB=%d allocated\n", idx);
                memset(p_l2c_ccb, 0, sizeof(*p_l2c_ccb));
                p_l2c_ccb->in_use = TRUE;
                p_l2c_ccb->local_cid = p_l2c_stream->local_cid;
                p_l2c_ccb->remote_cid = p_l2c_stream->remote_cid;
                p_l2c_ccb->out_mtu = p_l2c_stream->out_mtu;
                p_l2c_ccb->handle = p_l2c_stream->handle;
                p_l2c_ccb->is_flushable = p_l2c_stream->is_flushable;
                break;
            }
        }
    }

    /* If Not found ot not allocated */
    if (idx == BTM_SYNC_INFO_NUM_STR)
    {
        BTUSB_ERR("No Free L2C CCB found (handle=0x%x)\n", p_l2c_stream->handle);
        return -1;
    }

    /* Update Transmit Quota */
    p_l2c_ccb->link_xmit_quota = p_l2c_stream->link_xmit_quota;

    return 0;
}

/*******************************************************************************
**
** Function         btusb_lite_l2c_remove
**
** Description      Synchronize (Remove) L2CAP Stream
**
** Returns          Status.
**
*******************************************************************************/
int btusb_lite_l2c_remove(struct btusb *p_dev, UINT16 local_cid)
{
    int idx;
    struct btusb_lite_l2c_cb *p_l2c = &p_dev->lite_cb.s.l2c;
    struct btusb_lite_l2c_ccb *p_l2c_ccb;

    /* Check if this L2CAP Stream exists */
    for (idx = 0, p_l2c_ccb = p_l2c->ccb ; idx < ARRAY_SIZE(p_l2c->ccb) ; idx++, p_l2c_ccb++)
    {
        if ((p_l2c_ccb->in_use) &&
            (p_l2c_ccb->local_cid == local_cid))
        {
            break;
        }
    }
    /* If Not found */
    if (idx == BTM_SYNC_INFO_NUM_STR)
    {
        BTUSB_ERR("L2C CCB found (lcid=0x%x)\n",local_cid);
        return -1;
    }

    BTUSB_INFO("CCB=%d freed\n", idx);

    /* Reset (Free) the L2CAP Stream */
    memset(p_l2c_ccb, 0, sizeof(*p_l2c_ccb));

    return 0;
}

/*******************************************************************************
**
** Function         btusb_lite_l2c_send
**
** Description      Send L2CAP packet
**
** Returns          Status.
**
*******************************************************************************/
int btusb_lite_l2c_send(struct btusb *p_dev, BT_HDR *p_msg, UINT16 local_cid)
{
    int idx;
    struct btusb_lite_l2c_cb *p_l2c;
    struct btusb_lite_l2c_ccb *p_l2c_ccb;
    UINT8 *p_data;
    BT_HDR *backup_buffer;
    BT_HDR *p_msg_dup;
    static int queue_len = 0;
    static atomic_t lock;

    /* Temporary sync mechanism; will be changed*/
    if(atomic_read(&lock))
        atomic_set(&lock, 255);
    else
        atomic_set(&lock, 1);

    GKI_disable();
    /* Look for the first AV stream Started */
    p_l2c = &p_dev->lite_cb.s.l2c;
    for (idx = 0, p_l2c_ccb = p_l2c->ccb ; idx < BTM_SYNC_INFO_NUM_STR ; idx++, p_l2c_ccb++)
    {
        if (p_l2c_ccb->local_cid == local_cid)
        {
            break;
        }
    }

    if (idx == BTM_SYNC_INFO_NUM_STR)
    {
        GKI_enable();
    	BTUSB_ERR("No L2C CCB found (lcid=0x%x)\n", local_cid);
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        atomic_set(&lock, 0);
        return -1;
    }

    /*
     * Check if we have anything in Backup buffer if yes, push the new data in buffer
     * 		we will send the oldest data from the backup queue first
     * Check if the Tx Quota has been reached for this channel, push the data in
     * 		queue and exit.
     * Check if any other thread is already accesing this function: If yes and current
     *  	thread is callback thread, push the data in back up buffer and exit. If
     *  	current thread is completion handler thread, take out the data in backup
     *  	buffer and send.
     * */
    if (!GKI_queue_is_empty(&p_dev->back_queue) || (p_l2c_ccb->tx_pending >= p_l2c_ccb->link_xmit_quota) || (atomic_read(&lock) == 255))
    {
    	GKI_enable();

    	/* This case is when completion handler thread calld this function*/
    	if(p_msg == NULL)
    	{
    		BTUSB_ERR("Completion Thread\n");
    		goto clean;
    	}

    	/* Backing up of data*/
		backup_buffer = (BT_HDR *)GKI_getbuf(sizeof(BT_HDR) + p_msg->len + p_msg->offset - 1);
		if(!backup_buffer)
		{
			BTUSB_ERR("No Buffer available: Drop this packet\n");
			GKI_freebuf(p_msg); /* Free the original ACL buffer */
			atomic_set(&lock, 0);
			return -1;
		}
		memcpy((UINT8 *)backup_buffer, p_msg, (sizeof(BT_HDR) + p_msg->len + p_msg->offset - 1));
		GKI_enqueue(&p_dev->back_queue, backup_buffer);

		if(p_msg)
			GKI_freebuf(p_msg); /* Free the original ACL buffer */

		/* Exit the callback thread*/
		if(atomic_read(&lock) == 255)
		{
			BTUSB_ERR("Queue len:%d\n", queue_len);
			atomic_set(&lock, 0);
			return -1;
		}

		if(p_l2c_ccb->tx_pending >= p_l2c_ccb->link_xmit_quota)
		{
			BTUSB_ERR("Tx Quota reached(%d out of %d) for L2CAP channel (lcid=0x%x). Drop buffer. Queue Len:%d\n",
					p_l2c_ccb->tx_pending, p_l2c_ccb->link_xmit_quota, local_cid, p_dev->back_queue.count);
			/* Don't allow more than two buffers in the queue */
			if(p_dev->back_queue.count > 2)
			{
				// take the oldest msg from queue
				p_msg_dup = (BT_HDR *)GKI_dequeue(&p_dev->back_queue);
				if(p_msg_dup)
					GKI_freebuf(p_msg_dup); /* Free this ACL buffer */
			}
			atomic_set(&lock, 0);
			return -1;
		}
		else
		{
clean:		p_msg_dup = (BT_HDR *)GKI_dequeue(&p_dev->back_queue);
			if(!p_msg_dup)
			{
				BTUSB_ERR("No packets in queue\n");
				atomic_set(&lock, 0);
				return -1;
			}
		}
    }
    else
    {
    	p_msg_dup = p_msg;
    	GKI_enable();
    }
    /* Sanity */
    if (p_msg_dup->offset < BTUSB_LITE_L2CAP_HDR_SIZE)
    {
        BTUSB_ERR("offset too small=%d\n", p_msg_dup->offset);
        GKI_freebuf(p_msg_dup); /* Free this ACL buffer */
        atomic_set(&lock, 0);
        return-1;
    }

    /* Decrement offset to add headers */
    p_msg_dup->offset -= BTUSB_LITE_L2CAP_HDR_SIZE;

    /* Get address of the HCI Header */
    p_data = (UINT8 *)(p_msg_dup + 1) + p_msg_dup->offset;

    /* Write L2CAP Header (length field is SBC Frames + RTP/A2DP/Media Header) */
    p_data = btusb_lite_l2c_write_header(p_data, p_msg_dup->len, p_l2c_ccb->remote_cid);

    /* Increment length */
    p_msg_dup->len += BTUSB_LITE_L2CAP_HDR_SIZE;

    GKI_disable();      /* tx_pending field can be updated by another context */
    p_l2c_ccb->tx_pending++;            /* One more A2DP L2CAP packet pending */
    GKI_enable();
    BTUSB_DBG("L2C Tx Pending=%d\n", p_l2c_ccb->tx_pending);

    if (btusb_lite_hci_acl_send(p_dev, p_msg_dup, p_l2c_ccb->handle) < 0)
    {
        GKI_disable();      /* tx_pending field can be updated by another context */
        p_l2c_ccb->tx_pending--;        /* Remove A2DP L2CAP packet pending */
        GKI_enable();
        atomic_set(&lock, 0);
        return -1;
    }
	atomic_set(&lock, 0);
    p_l2c_ccb->link_tx_jiffies = jiffies;
    return 0;
}

/*******************************************************************************
 **
 ** Function         btusb_lite_l2c_write_header
 **
 ** Description      Write L2CAP ACL Header (Length, Channel Id)
 **
 ** Returns          New buffer location
 **
 *******************************************************************************/
static UINT8 *btusb_lite_l2c_write_header(UINT8 *p_data, UINT16 length, UINT16 cid)
{
    UINT16_TO_STREAM(p_data, length);               /* Length */
    UINT16_TO_STREAM(p_data, cid);                  /* Channel Id */
    return p_data;
}

/*******************************************************************************
 **
 ** Function         btusb_lite_l2c_nocp_hdlr
 **
 ** Description      L2CAP NumberOfcompletePacket Handler function
 **
 ** Returns          Number Of Complete Packet caught
 **
 *******************************************************************************/
UINT16 btusb_lite_l2c_nocp_hdlr(struct btusb *p_dev, UINT16 con_hdl, UINT16 num_cplt_pck)
{
    struct btusb_lite_l2c_cb *p_l2c;
    struct btusb_lite_l2c_ccb *p_l2c_ccb;
    int i;
    int delta;
    UINT16 num_cplt_pck_caugth;
    UINT8 msg[1] = {0}; /* dummy message */

    /* Look for the L2CAP channel matching the Connection Handle */
    p_l2c = &p_dev->lite_cb.s.l2c;
    for (i = 0, p_l2c_ccb = p_l2c->ccb ; i < BTM_SYNC_INFO_NUM_STR ; i++, p_l2c_ccb++)
    {
        if (p_l2c_ccb->handle == con_hdl)
        {
            break;
        }
    }
    /* If L2CAP channel not found/known */
    if (i == BTM_SYNC_INFO_NUM_STR)
    {
    	BTUSB_DBG("L2CAP channel not found\n");
        return 0;
    }

    /* If no Tx Pending */
    if (p_l2c_ccb->tx_pending == 0)
    {
		BTUSB_DBG("No tx pending\n");
        p_l2c_ccb->link_tx_jiffies = 0;
        return 0;
    }

    GKI_disable();      /* tx_pending field can be updated by another context */

    /* Take the min between the number of pending packet and the number of acked packet */
    num_cplt_pck_caugth = min(p_l2c_ccb->tx_pending, num_cplt_pck);

    /* Update the number of pending packet */
    p_l2c_ccb->tx_pending-= num_cplt_pck_caugth;
	
	 GKI_enable();
    /*This completion handler thread will check if any data is present in buffer*/
    if(!GKI_queue_is_empty(&p_dev->back_queue))
    {
    	i = btusb_lite_l2c_send(p_dev, NULL, p_l2c_ccb->local_cid);
    	BTUSB_ERR("Clearing queue:%d, res:%d, Tx Pending:%d\n", p_dev->back_queue.count, i, p_l2c_ccb->tx_pending);
    }
	
	GKI_disable(); 

    if (p_l2c_ccb->link_tx_jiffies)
    {
        delta = (int)jiffies_to_msecs(jiffies - p_l2c_ccb->link_tx_jiffies);
        BTUSB_DBG("L2C NOCP jiffies:%ld tx_jif:%d delta:%d\n",
                jiffies, p_l2c_ccb->link_tx_jiffies, delta);
       // if ((4500 < delta) && (delta < 5000))
        if (4500 < delta)
        {
            GKI_enable();

            /* need to reset */
            BTUSB_ERR("#####################L2C NOCP RESET!!!!!###########\n");
            /* trigger module reset */
            btusb_lite_ipc_rsp_send(p_dev, BT_EVT_BTU_IPC_L2C_EVT, L2C_HARDWARE_ERROR_EVT, msg, 1);
            return num_cplt_pck_caugth;
        }
    }
    BTUSB_DBG("L2C NOCP Tx Pending=%d\n", p_l2c_ccb->tx_pending);

    GKI_enable();

    return num_cplt_pck_caugth;
}
