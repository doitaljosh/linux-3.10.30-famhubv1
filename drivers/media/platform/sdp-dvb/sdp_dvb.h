/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the term of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SDP_DVB_H
#define _SDP_DVB_H

#if IS_ENABLED(CONFIG_DVB_SDP)

extern struct dvb_adapter *sdp_dvb_get_adapter(void);

#else /* CONFIG_DVB_SDP */

static inline struct dvb_adapter *sdp_dvb_get_adapter(void)
{
	return NULL;
}

#endif /* CONFIG_DVB_SDP */

#endif /* _SDP_DVB_H */
