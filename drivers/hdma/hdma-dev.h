#ifndef __HDMA_DEV_H
#define __HDMA_DEV_H

enum hdma_state {
	HDMA_IS_ON = 0,
	HDMA_IS_OFF,
	HDMA_IS_MIGRATING,
};

#ifdef CONFIG_HDMA_DEVICE
extern enum hdma_state get_hdma_status(void);
#else
static inline enum hdma_state get_hdma_status(void)
{
	return HDMA_IS_OFF;
}
#endif
int set_hdma_status(enum hdma_state state);

extern void hdma_regions_reserve(void);
extern unsigned long hdma_declared_pages(void);
extern int hdma_is_page_reserved(struct page *);

#endif
