/******************************************************************
* 		File : sdp_module_linker.c
*		Description : 
*		Author : tukho.kim@samsung.com		
*******************************************************************/

/******************************************************************
* 17/June/2014, working verison, tukho.kim, created
*******************************************************************/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <misc/sdp_module_linker.h>

#define NUM_ARG_MAX		(9)
#define NAME_LEN_MAX	(32)
#define FMT_LEN_MAX		(32)

struct  mlinker_type_t {
//	int type;
	union {
		int (*func_void) (void);
		int (*func_type1)(int);
		int (*func_type2)(int, int);
		int (*func_type3)(int, int, int);
		int (*func_type4)(int, int, int, int);
		int (*func_type5)(int, int, int, int, int);
		int (*func_type6)(int, int, int, int, int, int);
		int (*func_type7)(int, int, int, int, int, int, int);
		int (*func_type8)(int, int, int, int, int, int, int, int);
		int (*func_type9)(int, int, int, int, int, int, int, int, int);
	};
};

struct mlinker_entry_t {
	struct list_head list;

	atomic_t	ref_cnt;

	bool 			mutex_flag;
	struct mutex 	pfn_mutex;
	struct mlinker_entry_t *self;

	size_t 	name_len;
	size_t	fmt_len;

	const void 	*pfn;
	char 	fmt[FMT_LEN_MAX];
	char 	name[NAME_LEN_MAX];
};

struct mlinker_t {		// mod-function manager
	struct list_head list;
	spinlock_t list_lock;
};

static struct mlinker_t mlinker;		// mlinker resource


int sdp_invoke_mlinker(sdp_mlinker_hndl *phndl,
								const char *name, 
								const char *fmt,
								...) __attribute__ ((format (printf, 3, 4)));


int 
sdp_register_mlinker (const u32 	attr, 			// attribute
					const void *function, 		// function pointer
					const char *fmt,			// function argument format
					const char *name)			// block name - mfd, uddec, or chip name

{
	int ret_val = 0;
	unsigned long flags;

	size_t name_len;
	size_t fmt_len;
	struct mlinker_entry_t *mlinker_entry;
	struct list_head * entry;
	
	if (!function || !name){
		printk(KERN_ERR "[%s] register failed, function/name NULL\n", __FUNCTION__);
		return -EINVAL;
	}

	name_len = strlen(name);
	if (name_len > (NAME_LEN_MAX - 1)) {
		printk(KERN_ERR "[%s] register failed, name length max %d\n", __FUNCTION__, NAME_LEN_MAX-1);
		return -EINVAL;
	}

	fmt_len = (fmt == NULL) ? 0 : strlen(fmt);
	if (fmt_len > (FMT_LEN_MAX - 1)) {
		printk(KERN_ERR "[%s] register failed, fmt length max %d\n", __FUNCTION__, FMT_LEN_MAX-1);
		return -EINVAL;
	}

// check, it is already registered 
	spin_lock_irqsave(&mlinker.list_lock, flags);
	list_for_each(entry, &mlinker.list){
		mlinker_entry = list_entry(entry, struct mlinker_entry_t, list);	
		if(mlinker_entry->pfn == function){
			spin_unlock_irqrestore(&mlinker.list_lock, flags);
			printk(KERN_WARNING "[%s] already registered by %s\n", __FUNCTION__, name);
			return -EMLINK;
		}
	}
	spin_unlock_irqrestore(&mlinker.list_lock, flags);

// allocation
	mlinker_entry = kzalloc(sizeof(*mlinker_entry), GFP_KERNEL);	
	if(!mlinker_entry){
		printk(KERN_ERR "[%s] register failed, allocation failed\n", __FUNCTION__);
		ret_val = -ENOMEM;
		goto __mlinker_register_err;
	}

	mlinker_entry->pfn = function;
	mlinker_entry->self = mlinker_entry;
	mlinker_entry->name_len = name_len;
	mlinker_entry->fmt_len = fmt_len;
	memset(mlinker_entry->name,'\0', sizeof(mlinker_entry->name));
	memcpy(mlinker_entry->name, name, name_len);
	memset(mlinker_entry->fmt,'\0', sizeof(mlinker_entry->fmt));
	if(fmt_len) {	
		memcpy(mlinker_entry->fmt, fmt, fmt_len);
	}

	if(attr & SDP_MLINKER_MUTEX) {
		mlinker_entry->mutex_flag = true;
		mutex_init(&mlinker_entry->pfn_mutex);
	} else {
		mlinker_entry->mutex_flag = false;
	}

	spin_lock_irqsave(&mlinker.list_lock, flags);
	list_add(&mlinker_entry->list, &mlinker.list);
	spin_unlock_irqrestore(&mlinker.list_lock, flags);

	return 0;

__mlinker_register_err:
	if(mlinker_entry) {
		kfree(mlinker_entry);	
	}

	return ret_val;
}
EXPORT_SYMBOL(sdp_register_mlinker);

void 
sdp_unregister_mlinker(const void *function)
{
	unsigned long flags;

	struct list_head * entry;
	struct mlinker_entry_t *mlinker_entry;
	
	list_for_each(entry, &mlinker.list){
		mlinker_entry = list_entry(entry, struct mlinker_entry_t, list);	
		if (mlinker_entry->pfn == function){
			break;
		} else {
			mlinker_entry = NULL;
		}
	}

	if(!mlinker_entry){
		return;	
	}

	printk(KERN_DEBUG "[%s]%s\n", __FUNCTION__, mlinker_entry->name);

	spin_lock_irqsave(&mlinker.list_lock, flags);
	mlinker_entry->self = NULL;
	list_del(entry);
	spin_unlock_irqrestore(&mlinker.list_lock, flags);

	do{
		if(mlinker_entry->ref_cnt.counter == 0){
			break;	
		}
		yield();
	}while(1);

	kfree(mlinker_entry);
}

EXPORT_SYMBOL(sdp_unregister_mlinker);

union arg_type {
	int				i;
	char			c;
	short			h;
	long			l;
	long long		ll;
	unsigned int 	ui;	
	unsigned char	uc;
	unsigned short	uh;
	unsigned long	ul;
	unsigned long long	ull;
	size_t 			size;
//	intmax_t		intmax;
	ptrdiff_t 		ptrdiff;

	void 			*p;
	char			*cp;
	short			*hp;
	long			*lp;
	unsigned int 	*uip;	
	unsigned char	*ucp;
	unsigned short	*uhp;
	unsigned long	*ulp;
	unsigned long long	*ullp;
};



static int 
sdp_mlinker_arg(va_list args, const char *fmt, union arg_type * arg)
{
	int n = 0;
	int	type_len = sizeof(int);
	const bool machine_64bit = (sizeof(int) == 8) ? true : false;

	while(*fmt != '\0') {
		while(*fmt++ != '%') {
			if(*fmt	== '\0') {
				break;	
			}
		}
//lenth 
		switch(*fmt){
		case 'h':
			fmt++;		// short
			type_len = sizeof(short);
			if(*fmt == 'h'){
				fmt++; 	// char
				type_len = sizeof(char);
			}
			break;
		case 'l':
			fmt++;
			type_len = sizeof(long);		
			if((machine_64bit) && (*fmt == 'l')){  // long long
				fmt++; 	
				type_len = sizeof(long long);
			}
			break;
		case 'z':
			fmt++;
			type_len = sizeof(size_t);
			break;
		case 't':
			fmt++;
			type_len = sizeof(ptrdiff_t);		// ????
			break;
		case 'j':		// intmax_t		 not support in kernel
		case 'L':		// long double	 not support in kernel
		default:
			break;
		}

// type
		switch (*fmt){
		case 'c':		// char 	
			arg[n++].c = va_arg(args ,int);
			break;
		case 's':		// char * 
			arg[n++].cp = va_arg(args ,unsigned char*);
			break;
		case 'd':		// integer
		case 'i':		
			switch (type_len){
			case (sizeof(char)):
				arg[n++].c = (char)va_arg(args ,int);
				break;
			case (sizeof(short)):
				arg[n++].h = (short)va_arg(args ,int);
				break;
			case (sizeof(long)):
				arg[n++].l = (long)va_arg(args ,long);
				break;
			case (sizeof(long long)):
				arg[n++].ll = (long long)va_arg(args ,long long);
				break;
			default:
				arg[n++].i = va_arg(args ,int);
				break;
			}
			break;
		case 'x':		
		case 'u':		// hex
			switch (type_len){
			case (sizeof(char)):
				arg[n++].uc = (unsigned char)va_arg(args, unsigned int);
				break;
			case (sizeof(short)):
				arg[n++].uh = (unsigned short)va_arg(args, unsigned int);
				break;
			case (sizeof(long)):
				arg[n++].ul = (unsigned long)va_arg(args ,unsigned long);
				break;
			case (sizeof(long long)):
				arg[n++].ull = (unsigned long long)va_arg(args ,unsigned long long);
				break;
			default:
				arg[n++].ui = (unsigned int)va_arg(args ,unsigned int);
				break;
			}
			break;
		case 'p':		// void *
			arg[n++].p = va_arg(args ,void *);
			break;	
		default:		// skip 
			type_len = sizeof(int);
			continue;
			break;	
		}
		fmt++;	
	}

	return n;
}


int 
sdp_invoke_mlinker(sdp_mlinker_hndl *phndl, 
					const char *name,
					const char *fmt,
					...)
{
	int ret_val = 0;

//	unsigned long flags;
	struct mlinker_entry_t *mlinker_entry = NULL;
	struct mlinker_type_t func;

	struct list_head * entry;

	va_list args;
	int 	num_arg;
	union 	arg_type arg[NUM_ARG_MAX];
	
	if(phndl && *phndl){
		mlinker_entry = (struct mlinker_entry_t *)*phndl;
		if(mlinker_entry != mlinker_entry->self) {
			mlinker_entry = NULL;
		}else{
			atomic_inc(&mlinker_entry->ref_cnt);
		}
	}

	if(!mlinker_entry) {
		if(!name){
			return -EINVAL;	
		}

		list_for_each(entry, &mlinker.list){		// for loop
			mlinker_entry = list_entry(entry, struct mlinker_entry_t, list);	
			if(strncmp(mlinker_entry->name, name, mlinker_entry->name_len) == 0) {
				atomic_inc(&mlinker_entry->ref_cnt);
				break;	
			} else {
				mlinker_entry = NULL;	
			}
		}
	}

	if(!mlinker_entry) {
		return -ENXIO;		// no address
	}

	if(mlinker_entry != mlinker_entry->self)
	{
		ret_val = -ENXIO;	
		goto __out_invoke_function;
	}

	func.func_void = mlinker_entry->pfn;
	if(!func.func_void){
		ret_val = -ENXIO;	
		goto __out_invoke_function;
	}

	if(strncmp(mlinker_entry->fmt, fmt, mlinker_entry->fmt_len) != 0) {
		ret_val = -EINVAL;		// format not match
		goto __out_invoke_function;
	}

	if(phndl) {
//		printk("phndl %p\n", phndl);
		*phndl = (void*)mlinker_entry;
	}

	va_start(args, fmt);
	num_arg = sdp_mlinker_arg(args, fmt, arg);
	va_end(args);

	if(mlinker_entry->mutex_flag == true){
		mutex_lock(&mlinker_entry->pfn_mutex);
	}

// remove num args 	switch(mlinker_entry->num_args) {

	switch(num_arg) {
	case 9:
		ret_val = func.func_type9(arg[0].i, arg[1].i, arg[2].i, arg[3].i, arg[4].i, arg[5].i, arg[6].i, arg[7].i, arg[8].i);
		break;
	case 8:
		ret_val = func.func_type8(arg[0].i, arg[1].i, arg[2].i, arg[3].i, arg[4].i, arg[5].i, arg[6].i, arg[7].i);
		break;
	case 7:
		ret_val = func.func_type7(arg[0].i, arg[1].i, arg[2].i, arg[3].i, arg[4].i, arg[5].i, arg[6].i);
		break;
	case 6:
		ret_val = func.func_type6(arg[0].i, arg[1].i, arg[2].i, arg[3].i, arg[4].i, arg[5].i);
		break;
	case 5:
		ret_val = func.func_type5(arg[0].i, arg[1].i, arg[2].i, arg[3].i, arg[4].i);
		break;
	case 4:
		ret_val = func.func_type4(arg[0].i, arg[1].i, arg[2].i, arg[3].i);
		break;
	case 3:
		ret_val = func.func_type3(arg[0].i, arg[1].i, arg[2].i);
		break;
	case 2:
		ret_val = func.func_type2(arg[0].i, arg[1].i);
		break;
	case 1:
		ret_val = func.func_type1(arg[0].i);
		break;
	case 0:
		ret_val = func.func_void();
		break;
	default:
		ret_val = -EINVAL;
		break;
	}

	if(mlinker_entry->mutex_flag == true){
		mutex_unlock(&mlinker_entry->pfn_mutex);
	}

__out_invoke_function:
	atomic_dec(&mlinker_entry->ref_cnt);
	
	return ret_val;
}
EXPORT_SYMBOL(sdp_invoke_mlinker);

void 
sdp_inform_mlinker(const sdp_mlinker_hndl hndl)
{
	struct list_head *entry;
	struct mlinker_entry_t * mlinker_entry = NULL;

	list_for_each(entry, &mlinker.list){
		mlinker_entry = list_entry(entry, struct mlinker_entry_t, list);	
		if(!hndl){
			printk(KERN_INFO "[MODULE_LINKER] %s registered\n", mlinker_entry->name);
		} else if(mlinker_entry == hndl) {
			printk(KERN_INFO "[MODULE_LINKER] %s registered\n", mlinker_entry->name);
			break;	
		} else {
			mlinker_entry = NULL;	
		}
	}
	
	if(!mlinker_entry){
		printk(KERN_INFO "[MODULE_LINKER] not registered\n");
	}
}
EXPORT_SYMBOL(sdp_inform_mlinker);

#if 0
static int testfn(int arg0, int arg1, int arg2, int arg3, int arg4)
{

	printk("arg0 is %u\n", (unsigned int)arg0);
	printk("arg1 is %s\n", (const char *)arg1);
	printk("arg2 is %c\n", (char)arg2);
	printk("arg3 is %p\n", (void*)arg3);
	printk("arg4 is %zu\n", (unsigned int)arg4);

	return 0;
}
#endif

static int __init
sdp_mlinker_init (void)
{
	int ret = 0;

	memset(&mlinker, 0, sizeof(mlinker));
	INIT_LIST_HEAD(&mlinker.list);		// list init
	spin_lock_init(&mlinker.list_lock);

// test
#if 0
{
	static sdp_mlinker_hndl hndl;

	char * string = "call testfn";

	if(sdp_register_mlinker_mutex((void*)testfn, "%hhu %s %c %p %zu", "testfn") < 0) {
		printk("[%s] register failed %d\n", __FUNCTION__, ret);	
	} else {
		sdp_invoke_mlinker(&hndl, "testfn", "%hhu %s %c %p %zu", 1000, (char*)string, 'c', hndl, sizeof(mlinker));
		sdp_invoke_mlinker(&hndl, NULL, "%hhu %s %c %p %zu", 1000, (char*)string, 'c', hndl, sizeof(int));
//		sdp_unregister_mlinker((void*)testfn);
	}
}
#endif

//__init_error:
	return ret;
}
module_init(sdp_mlinker_init);

static void __exit
sdp_mlinker_exit(void)
{
	struct list_head *entry;
	struct mlinker_entry_t *mlinker_entry;

	do{
		mlinker_entry = NULL;
		list_for_each(entry, &mlinker.list){
			mlinker_entry = list_entry(entry, struct mlinker_entry_t, list);	
			break;
		}

		if(mlinker_entry){
			list_del(entry);
			kfree(mlinker_entry);
		}
	}while(mlinker_entry != NULL); 

}
module_exit(sdp_mlinker_exit);


MODULE_LICENSE("Proprietary");


