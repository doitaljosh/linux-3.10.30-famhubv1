/*
 * tvis_agent_packet.h
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * NOTE:
 *
 */

#ifndef _TVIS_AGENT_PACKET_H
#define _TVIS_AGENT_PACKET_H

#define CONFIG_BUFFER_SIZE  (4 * 1024)
#define RECV_BUF_SIZE	(8192 * 1024)

#define MAX_TOOLS	(256)
#define MAX_TOOL_INS	(65536)

#define TOOL_KDEBUGD_DEVICE		"/dev/agent.kdbgd"

#ifdef CONFIG_PLAT_TIZEN	/* TIZEN PLATFORM */
#define TOOL_KDEBUGD_ALT_DEVICE	"/opt/usr/apps/vdtools/agent.kdbgd"
#else	/* ORSAY PLATFORM */
#define TOOL_KDEBUGD_ALT_DEVICE "/mtd_rwarea/agent.kdbgd"
#endif

/* set of tools */
enum tool_list {
	TOOL_AGENT = 0,
	TOOL_DUMA,
	TOOL_DML,
	TOOL_KDEBUGD,
	TOOL_FTRACE,
	TOOL_MAX /* MAX no. of tools */
};

/*
 * TODO: Find a more suitable place for these functions
 * currently no other file is shared by all tools
 * (this is the common place)
 */
struct tool_list_map {
	enum tool_list tool;
	const char *name;
};

extern const struct tool_list_map g_list_map[TOOL_MAX];

static inline const char *get_tool_name(int tool)
{
	if ((tool < TOOL_AGENT)
			|| (tool > TOOL_FTRACE))
		return NULL;

	return g_list_map[tool].name;
}

/* packet types */
enum packet_type {
	/* Packet containing comand*/
	PACKET_CMD = 1,
	/* Packet containing data inresponse of command*/
	PACKET_DATA,
	/* ACK Packet*/
	PACKET_ACK,
	/* NACK Packet*/
	PACKET_NACK,
	/* This packet is used for sending internal command to Tools
	 * When ever agent send commond to tool
	 * Agent should use this kind of packet type */
	PACKET_INT
};

/* tool header format */
struct  tool_header{
	int32_t cmd;
	int32_t data_len;
};

#define TOOL_HDR_SIZE	(sizeof(struct  tool_header))

/* tvis agent header format */
struct  tvis_agent_header{
	int8_t type;
	int8_t tool;
	int16_t tool_ins;
	int32_t payload_len;
};

#define TVIS_AGENT_HDR_SIZE	(sizeof(struct  tvis_agent_header))

/* tvis agent packet format */
struct tvis_agent_packet{
	struct  tvis_agent_header ta_hdr;
	struct tool_header t_hdr;
};

extern char *recv_buf; /* input packet buffer */
extern char *send_buf; /* output packet buffer */

int tvis_agent_packet_init(void);
void tvis_agent_packet_exit(void);
int tvis_cmd_data_handler(struct tvis_agent_header *ta_hdr, struct tool_header *t_hdr);
int tool_data_handler(struct tvis_agent_header *ta_hdr, struct tool_header *t_hdr);
#ifdef COMMON_DEBUG
extern void tvis_agent_hexdump(char *buf, int len);
#else
static inline void tvis_agent_hexdump(char *buf, int len) {}
#endif

#endif
