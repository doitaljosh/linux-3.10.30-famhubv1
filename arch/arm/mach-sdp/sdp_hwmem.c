/*********************************************************************************************
 *
 *	sdp_hwmem.c (Samsung Soc DMA memory allocation)
 *
 *	author : tukho.kim@samsung.com
 *	
 ********************************************************************************************/
/*********************************************************************************************
 * Description 
 * Date 	author		Description
 * ----------------------------------------------------------------------------------------
// Jul,09,2010 	tukho.kim	created
 ********************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>		
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/jiffies.h> 
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/memory.h>		// alloc_page_node
#include <asm/uaccess.h>	// copy_from_user
#include <asm-generic/mman.h>	// copy_from_user
#include <asm/cacheflush.h>	// copy_from_user

#include <mach/sdp_hwmem_io.h>
#include <mach/soc.h>
#include "common.h"

#undef DEBUG_SDP_HWMEM

#ifdef DEBUG_SDP_HWMEM
#define DPRINT_SDP_HWMEM(fmt, args...) printk("[%s]" fmt, __FUNCTION__, ##args)
#else
#define DPRINT_SDP_HWMEM(fmt, args...) 
#endif 

#define PRINT_HWMEM_ERR(fmt, args...) printk(KERN_ERR"[%s]" fmt, __FUNCTION__, ##args)

#define DRV_HWMEM_NAME		"sdp_hwmem"
#define SDP_HWMEM_MINOR		192

#define DRV_MEM_NAME		"sdp_mem"
#define SDP_MEM_MINOR		193

static struct proc_dir_entry *sdpver;

#ifdef CONFIG_OF
EXPORT_SYMBOL(sdp_get_mem_cfg);
#endif

#ifdef CONFIG_T2D_DEBUGD
#include <linux/delay.h>
#include <t2ddebugd/t2ddebugd.h>
int t2ddebug_ddr_margin_check(void);
#endif


static int
sdp_hwmem_get(struct file * file, SDP_GET_HWMEM_T* p_hwmem_info)
{
	int	ret_val = 0;
	SDP_GET_HWMEM_T hwmem_info;

	struct page *page;
	unsigned long order, addr;
	size_t size;
	unsigned long populate;

	ret_val = (int) copy_from_user(&hwmem_info, p_hwmem_info, sizeof(SDP_GET_HWMEM_T));

	if(ret_val){
		PRINT_HWMEM_ERR("get hwmem info failed\n");
		return -EINVAL;
	}
	
	DPRINT_SDP_HWMEM("size: %d, %s\n", hwmem_info.size, (hwmem_info.uncached)?"uncached":"uncached");

	file->private_data = (void*)hwmem_info.uncached;

	size = PAGE_ALIGN(hwmem_info.size);
	order = (unsigned long) get_order(size);

	page = (hwmem_info.node < 0) ?
			alloc_pages(GFP_ATOMIC | GFP_DMA ,order):
			alloc_pages_node(hwmem_info.node, GFP_ATOMIC | GFP_DMA ,order);

	if(!page) {
		PRINT_HWMEM_ERR("alloc page failed\n");
		return -ENOMEM;
	}

	DPRINT_SDP_HWMEM("get pages: 0x%08x\n",(u32)page);

	hwmem_info.phy_addr = addr = (u32) page_to_phys(page);
	//hwmem_info.vir_addr = do_mmap(file, 0, size, PROT_WRITE | PROT_READ, MAP_SHARED, addr);
	hwmem_info.vir_addr = 
		do_mmap_pgoff(file, 0, size, PROT_READ | PROT_WRITE, MAP_SHARED, addr >> PAGE_SHIFT, &populate);
	if(IS_ERR((void *) hwmem_info.vir_addr))
	{
		PRINT_HWMEM_ERR("do_mmap_pgoff failed\n");
		return -ENOMEM;
	}
	if (populate)
		mm_populate(hwmem_info.vir_addr, populate);
	
	DPRINT_SDP_HWMEM("phy: 0x%08x, vir: 0x%08x\n",(u32)hwmem_info.phy_addr,(u32)hwmem_info.vir_addr);

	ret_val = (int) copy_to_user((void*)p_hwmem_info, (void*)&hwmem_info, sizeof(SDP_GET_HWMEM_T));

	return 0;
}

static int 
sdp_hwmem_rel(struct file* file, SDP_REL_HWMEM_T* p_hwmem_info)
{
	int	ret_val = 0;

	SDP_REL_HWMEM_T hwmem_info;

	struct mm_struct *mm = current->mm;
	unsigned long order;
	size_t 	size;

	ret_val = (int) copy_from_user(&hwmem_info, p_hwmem_info, sizeof(SDP_REL_HWMEM_T));

	if(ret_val){
		PRINT_HWMEM_ERR("get hwmem info failed\n");
		return -EINVAL;
	}

	size = PAGE_ALIGN(hwmem_info.size);

	do_munmap(mm, hwmem_info.vir_addr, size);
	order = (unsigned long) get_order(size);
	__free_pages(pfn_to_page(__phys_to_pfn(hwmem_info.phy_addr)), order);
	
	return 0;
}

#ifdef CONFIG_SCHED_HMP
extern void flush_all_cpu_caches(void);

void sdp_hwmem_flush_all(void)
{
	flush_all_cpu_caches();
}
#else
void sdp_hwmem_flush_all(void)
{
	unsigned long flag;

	raw_local_irq_save (flag);

#ifdef CONFIG_SMP
	raw_local_irq_restore (flag);

	smp_call_function((smp_call_func_t) __cpuc_flush_kern_louis, NULL, 1);

	raw_local_irq_save (flag);
#endif

	flush_cache_all ();

	outer_flush_all(); 

	raw_local_irq_restore (flag);
}
#endif
EXPORT_SYMBOL(sdp_hwmem_flush_all);

static inline void 
sdp_hwmem_inv_range(const void* vir_addr, const unsigned long phy_addr, const size_t size)
{
	dmac_map_area (vir_addr, size, DMA_FROM_DEVICE);
	outer_inv_range(phy_addr, (unsigned long)(phy_addr+size));
}

static inline void 
sdp_hwmem_clean_range(const void* vir_addr, const unsigned long phy_addr, const size_t size)
{
	dmac_map_area (vir_addr, size, DMA_TO_DEVICE);
	outer_clean_range(phy_addr, (unsigned long)(phy_addr+size));
}

static inline void 
sdp_hwmem_flush_range(const void* vir_addr, const unsigned long phy_addr, const size_t size)
{
	dmac_flush_range(vir_addr,(const void *)((u32)vir_addr+size));
	outer_flush_range(phy_addr, (unsigned long)(phy_addr+size));
}

void 
sdp_gpu_hwmem_inv_range(const void* vir_addr, const size_t size)
{
	dmac_map_area (vir_addr, size, DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(sdp_gpu_hwmem_inv_range);

void 
sdp_gpu_hwmem_clean_range(const void* vir_addr, const size_t size)
{
	dmac_map_area (vir_addr, size, DMA_TO_DEVICE);
}
EXPORT_SYMBOL(sdp_gpu_hwmem_clean_range);

void 
sdp_gpu_hwmem_flush_range(const void* vir_addr, const size_t size)
{
	dmac_flush_range(vir_addr,(const void *)((u32)vir_addr+size));
}
EXPORT_SYMBOL(sdp_gpu_hwmem_flush_range);

void sdp_irq_set_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	irq_set_affinity(irq, cpumask);
}
EXPORT_SYMBOL(sdp_irq_set_affinity);


static const struct vm_operations_struct mmap_mem_ops = {
	.open = NULL,
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys
#endif
};

static inline int __intersects(phys_addr_t addr, phys_addr_t size, struct memblock_region *reg)
{
	if (addr >= reg->base + reg->size)
		return 1;
	else if (addr + size <= reg->base)
		return -1;
	return 0;
}

static bool memblock_intersects(struct memblock_type *type, phys_addr_t addr, phys_addr_t size)
{
	unsigned int left = 0, right = type->cnt;

	do {
		unsigned int mid = (right + left) / 2;

		int intersect_res = __intersects(addr, size, &type->regions[mid]);

		/* left side */
		if (intersect_res < 0)
			right = mid;
		/* right side */
		else if (intersect_res > 0)
			left = mid + 1;
		/* found intersection */
		if (intersect_res == 0)
			return true;

	} while (left < right);

	return false;
}

static int sdp_hwmem_mmap(struct file * file, struct vm_area_struct * vma)
{
	size_t size = vma->vm_end - vma->vm_start;

	if (memblock_intersects(&memblock.memory, (phys_addr_t) (vma->vm_pgoff) << PAGE_SHIFT , (phys_addr_t) size))
		return -EFAULT;

	if (file->f_flags & O_SYNC)
#ifdef CONFIG_ARM_ERRATA_821423
		vma->vm_page_prot = __pgprot_modify(vma->vm_page_prot,
				L_PTE_MT_MASK, L_PTE_MT_UNCACHED | L_PTE_XN);
#else
		vma->vm_page_prot = __pgprot_modify(vma->vm_page_prot,
				L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE | L_PTE_XN);
#endif

	vma->vm_ops = &mmap_mem_ops;

	/* Remap-pfn-range will mark the range VM_IO and VM_RESERVED */
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
                        	size, vma->vm_page_prot);
}

static int 
sdp_hwmem_cache(unsigned int cmd, SDP_CACHE_HWMEM_T * p_hwmem_info)
{
	int ret_val = 0;
	SDP_CACHE_HWMEM_T hwmem_info;

	ret_val = (int) copy_from_user(&hwmem_info, p_hwmem_info, sizeof(SDP_CACHE_HWMEM_T));

	if(ret_val){
		PRINT_HWMEM_ERR("get hwmem info failed\n");
		return -EINVAL;
	}

	switch(cmd) {
		case (CMD_HWMEM_INV_RANGE): 
			sdp_hwmem_inv_range((const void *)hwmem_info.vir_addr, 
								(const unsigned long)hwmem_info.phy_addr, 
								hwmem_info.size);
			break;
		case (CMD_HWMEM_CLEAN_RANGE): 
			sdp_hwmem_clean_range((const void *)hwmem_info.vir_addr, 
								(const unsigned long)hwmem_info.phy_addr, 
								hwmem_info.size);
			break;
		case (CMD_HWMEM_FLUSH_RANGE): 
			sdp_hwmem_flush_range((const void *)hwmem_info.vir_addr, 
								(const unsigned long)hwmem_info.phy_addr, 
								hwmem_info.size);
			break;
		default:
			break;
	}

	return 0;
}

static DEFINE_SPINLOCK(clkgating_lock);

int sdp_pmu_regset(void* reg_addr, u32 mask, u32 value)
{
	unsigned long flags;
	u32 tmp;
	
	spin_lock_irqsave(&clkgating_lock, flags);
	
	tmp = readl(reg_addr);
	tmp &= ~mask;
	tmp |= value;
	writel(tmp, reg_addr);

	spin_unlock_irqrestore(&clkgating_lock, flags);

	return 0;
	
}

/* XXX: clk-sdp.c has the same function for CONFIG_OF */
#if !defined(CONFIG_OF)
int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value)
{
	void *addr;

	if((phy_addr < SFR0_BASE) || (phy_addr >= (SFR0_BASE + SFR0_SIZE))) {
		printk("Address is not Vaild!!!!! addr=0x%08X\n", phy_addr);
		return -EINVAL;
	}

	addr = (void*)(DIFF_IO_BASE0 + phy_addr);
	return sdp_pmu_regset(addr, mask, value);
}
EXPORT_SYMBOL(sdp_set_clockgating);
#endif

static int
sdp_hwmem_set_clockgating(SDP_CLKGATING_HWMEM_T * args)
{
	int ret_val = 0;
	SDP_CLKGATING_HWMEM_T clk_info;

	ret_val = (int) copy_from_user(&clk_info, args, sizeof(SDP_CLKGATING_HWMEM_T));
	if(ret_val){
		PRINT_HWMEM_ERR("get clockgating info failed\n");
		return -EINVAL;
	}

	return sdp_set_clockgating(clk_info.phy_addr, clk_info.mask, clk_info.value);
}

static void sdp_write_ddr_mem(uint8_t *pMemAddr,uint32_t uMemSize,uint8_t *pPattern,uint32_t uPatternSize,int bCacheFlush)
{
	uint32_t uIdx;
	for(uIdx=0; uIdx < uMemSize; uIdx++)
	{
		pMemAddr[uIdx] = *(pPattern + (uIdx%uPatternSize));
	}

	if(bCacheFlush)
	{
		unsigned long start_addr = (unsigned long) pMemAddr;
		unsigned long end_addr   = start_addr + uMemSize;
		flush_cache_vmap(start_addr,end_addr);
	}
}

static int sdp_check_ddr_mem(uint8_t *pMemAddr,uint32_t uMemSize,uint8_t *pPattern,uint32_t uPatternSize)
{
	uint32_t uIdx;

	if (pMemAddr == NULL)
		return -1;
	
	for(uIdx=0; uIdx < uMemSize; uIdx++)
	{
		if(pMemAddr[uIdx] != *(pPattern + uIdx%uPatternSize))
		{
			PRINT_HWMEM_ERR("pMemAddr Invalid : [%p], IDX[%d], value[%d] compared value[%d]\n"
					,pMemAddr,uIdx,pMemAddr[uIdx],*(pPattern + uIdx%uPatternSize));
			return -1;
		}
	}

	return 0;
}

static int sdp_hwmem_ddr_check(SDP_HWMEM_DDR_CHECK_T* args)
{
	int ret_val;
	SDP_HWMEM_DDR_CHECK_T ddr_check;
	uint8_t *ddr_test_mem_p[SDP_DDR_MAX] = {NULL, };
	unsigned long end_time;
	uint8_t *write_pattern_p;
	uint32_t pattern_size;
	uint32_t counter = 0;
	phys_addr_t address;
	uint32_t data[3];
	uint32_t size;

	struct device_node *np_mem = of_find_node_by_name(NULL,"sdp_mmap");
	if (np_mem == NULL){
		PRINT_HWMEM_ERR("Could Not find sdp_mem_map entry\n");
		return -EINVAL;

	}

	ret_val = (int) copy_from_user(&ddr_check, args, sizeof(SDP_HWMEM_DDR_CHECK_T));
	if(ret_val){
		PRINT_HWMEM_ERR("get ddr check: get info from user space failed\n");
		return -EINVAL;
	}

	if(ddr_check.ddr_type == 0)
	{
		ddr_check.ddr_return.counter = 0;
		return -EINVAL;
	}

	if (ddr_check.test_size > (2 * 1024 * 1024)) {
		PRINT_HWMEM_ERR("ERROR: DDR check test_size cannot be more than 2 MBtyes \n");
		return -ENOMEM;
	}


	if(ddr_check.ddr_type & SDP_DDR_CHECK_0)
	{
		ret_val = of_property_read_u32_array(np_mem, "samsung,pvr", data, 3);
		if (ret_val)
		{
			PRINT_HWMEM_ERR("failed to parse samsung,pvr memory\n");
			//return ret;
		}

		address = (phys_addr_t)((uint64_t)data[0] << 32) | data[1];
		size = data[2];

		ddr_test_mem_p[SDP_DDR_0] = ioremap_nocache(address, size);
		if(!ddr_test_mem_p[SDP_DDR_0])
		{
			PRINT_HWMEM_ERR("Failed to get DDR0 memory for DDR check\n");
			return -ENOMEM;
		}

		ddr_check.ddr_return.sdp_ddr0_check_result = 1;
	}

	if(ddr_check.ddr_type & SDP_DDR_CHECK_1)
	{
#if defined(CONFIG_ARCH_SDP1406)
		if (soc_is_sdp1406fhd())
		{
			ret_val = of_property_read_u32_array(np_mem, "samsung,align0", data, 3); 
		}
		else
		{
			if (of_machine_is_compatible("samsung,sdp1406_2_5"))
				ret_val = of_property_read_u32_array(np_mem, "samsung,dp-vt-c", data, 3); 
			else
				ret_val = of_property_read_u32_array(np_mem, "samsung,align3", data, 3);
		}
#else // SDP1404
		ret_val = of_property_read_u32_array(np_mem, "samsung,dp-vt-c", data, 3);
#endif
		if (ret_val)
		{
			PRINT_HWMEM_ERR("failed to parse samsung,pvr memory\n");
			//return ret;
		}

		address = (phys_addr_t)((uint64_t)data[0] << 32) | data[1];
		size = data[2];

		ddr_test_mem_p[SDP_DDR_1] = ioremap_nocache(address, size);
		if(!ddr_test_mem_p[SDP_DDR_1])
		{
			PRINT_HWMEM_ERR("Failed to get DDR1 memory for DDR check\n");
			return -ENOMEM;
		}

		ddr_check.ddr_return.sdp_ddr1_check_result = 1;
	}	

	if(ddr_check.ddr_type & SDP_DDR_CHECK_2)
	{
#ifdef CONFIG_ARCH_SDP1406
		if (soc_is_sdp1406fhd())
		{
			PRINT_HWMEM_ERR("ERROR: None of HAWKM_UHD_1.5 or HAWKM_UHD_2.5 SOC chosen for DDR Check test\n");
			return -ENOMEM;
		}
		else // UHD
		{
			ret_val = of_property_read_u32_array(np_mem, "samsung,dp2", data, 3);
		}
#else
		PRINT_HWMEM_ERR("ERROR: None of HAWKM_UHD_1.5 or HAWKM_UHD_2.5 SOC chosen for DDR Check test\n");
		return -ENOMEM;
#endif
		if (ret_val)
		{
			PRINT_HWMEM_ERR("failed to parse samsung,pvr memory\n");
			//return ret;
		}

		address = (phys_addr_t)((uint64_t)data[0] << 32) | data[1];
		size = data[2];

		ddr_test_mem_p[SDP_DDR_2] = ioremap_nocache(address, size);
		if(!ddr_test_mem_p[SDP_DDR_2])
		{
			PRINT_HWMEM_ERR("Failed to get DDR2 memory for DDR check\n");
			return -ENOMEM;
		}

		ddr_check.ddr_return.sdp_ddr2_check_result = 1;
	}

	write_pattern_p = (uint8_t *)(&(ddr_check.write_pattern));
	pattern_size    = sizeof(ddr_check.write_pattern);
	end_time 		= jiffies + msecs_to_jiffies(ddr_check.test_time);
	do
	{
		// Write the test pattern in DDR 0
		if(ddr_check.ddr_type & SDP_DDR_CHECK_0)
			sdp_write_ddr_mem(ddr_test_mem_p[SDP_DDR_0],ddr_check.test_size,write_pattern_p,pattern_size,1);
		// Write the test pattern in DDR 1
		if(ddr_check.ddr_type & SDP_DDR_CHECK_1)
			sdp_write_ddr_mem(ddr_test_mem_p[SDP_DDR_1],ddr_check.test_size,write_pattern_p,pattern_size,1);
		// Write the test pattern in DDR 2
		if((ddr_check.ddr_type & SDP_DDR_CHECK_2) && (ddr_test_mem_p[SDP_DDR_2] != 0))
			sdp_write_ddr_mem(ddr_test_mem_p[SDP_DDR_2],ddr_check.test_size,write_pattern_p,pattern_size,1);


		// Read, compare and check the DDR 0
		if(ddr_check.ddr_return.sdp_ddr0_check_result && sdp_check_ddr_mem(ddr_test_mem_p[SDP_DDR_0],ddr_check.test_size,write_pattern_p,pattern_size) != 0)
			ddr_check.ddr_return.sdp_ddr0_check_result = 0;
		// Read, compare and check the DDR 1
		if(ddr_check.ddr_return.sdp_ddr1_check_result && sdp_check_ddr_mem(ddr_test_mem_p[SDP_DDR_1],ddr_check.test_size,write_pattern_p,pattern_size) != 0)
			ddr_check.ddr_return.sdp_ddr1_check_result = 0;
		// Read, compare and check the DDR 2
		if (ddr_test_mem_p[SDP_DDR_2] != 0)
		{
			if(ddr_check.ddr_return.sdp_ddr2_check_result && sdp_check_ddr_mem(ddr_test_mem_p[SDP_DDR_2],ddr_check.test_size,write_pattern_p,pattern_size) != 0)
				ddr_check.ddr_return.sdp_ddr2_check_result = 0;
		}

		counter++;
	}while(jiffies <= end_time);

	ddr_check.ddr_return.counter = counter;

	ret_val = (int) copy_to_user((void __user *) args, &ddr_check, sizeof(SDP_HWMEM_DDR_CHECK_T));
	if(ret_val){
		PRINT_HWMEM_ERR("send ddr check: set info to user space failed\n");
		return -EINVAL;
	}
	return 0;	
}

static long 
sdp_hwmem_ioctl(struct file *file, unsigned int cmd, unsigned long args)
{
	int ret_val = 0;

	switch(cmd){
		case (CMD_HWMEM_GET): 
			ret_val = sdp_hwmem_get(file, (SDP_GET_HWMEM_T*) args);
			break;
		case (CMD_HWMEM_RELEASE): 
			ret_val = sdp_hwmem_rel(file, (SDP_REL_HWMEM_T*) args);
			break;
		case (CMD_HWMEM_CLEAN): 
			break;
		case (CMD_HWMEM_FLUSH): 
			sdp_hwmem_flush_all();
			break;
		case (CMD_HWMEM_INV_RANGE): 
		case (CMD_HWMEM_CLEAN_RANGE): 
		case (CMD_HWMEM_FLUSH_RANGE): 
			ret_val = sdp_hwmem_cache(cmd, (SDP_CACHE_HWMEM_T *) args);
			break;
		case (CMD_HWMEM_GET_REVISION_ID):
			{
				unsigned int u32Val = (unsigned int) sdp_get_revision_id();
				ret_val = (int) copy_to_user((void __user *) args, &u32Val, sizeof(u32Val));
			}
			break;
		case (CMD_HWMEM_SET_CLOCKGATING): 
			ret_val = (int) sdp_hwmem_set_clockgating((SDP_CLKGATING_HWMEM_T *) args);
			break;
		case CMD_HWMEM_DDR_CHECK:
			ret_val = sdp_hwmem_ddr_check((SDP_HWMEM_DDR_CHECK_T *) args);
			break;			
		default:
			break;
	}	

	return ret_val;
}

static int sdp_hwmem_open(struct inode *inode, struct file *file)
{
	DPRINT_SDP_HWMEM("open\n");


	return 0;
}


static int sdp_hwmem_release (struct inode *inode, struct file *file)
{

	DPRINT_SDP_HWMEM("release\n");

	return 0;
}

static const struct file_operations sdp_hwmem_fops = {
	.owner = THIS_MODULE,
	.open  = sdp_hwmem_open,
	.release = sdp_hwmem_release,
	.unlocked_ioctl = sdp_hwmem_ioctl,
	.mmap = sdp_hwmem_mmap,
};

static struct miscdevice sdp_hwmem_dev = {
	.minor = SDP_HWMEM_MINOR,
	.name = DRV_HWMEM_NAME,
	.fops = &sdp_hwmem_fops	
};

static int proc_read_sdpver(struct seq_file *m, void *v)
{
	seq_printf(m, "ES%d\n", sdp_get_revision_id());
	return 0;
}

static int sdpver_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_sdpver, NULL);
}

static const struct file_operations sdp_proc_file_fops = {
	.open = sdpver_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};	

static int __init sdp_hwmem_init(void)
{
	int ret_val = 0;

	ret_val = misc_register(&sdp_hwmem_dev);

	if(ret_val){
		printk(KERN_ERR "[ERR]%s: misc register failed\n", DRV_HWMEM_NAME);
	}
	else {
		printk(KERN_INFO"[SDP_HWMEM] %s initialized\n", DRV_HWMEM_NAME);
	}

	sdpver = proc_create("sdp_version", S_IRUGO, NULL, &sdp_proc_file_fops);
	if(sdpver == NULL)
	{
		pr_err("[SDP_HWMEM] fail to create proc sdpver info\n");
	}
	else
	{
		pr_info("/proc/sdp_version is registered!\n");
	}

#ifdef CONFIG_T2D_DEBUGD
	t2d_dbg_register("DDR MARGIN CHECK",44,t2ddebug_ddr_margin_check, NULL);
#endif

	return ret_val;
}

static void __exit sdp_hwmem_exit(void)
{

	misc_deregister(&sdp_hwmem_dev);

	return;
}

#ifdef CONFIG_T2D_DEBUGD
#define DDR_MASK				((1 << 3) - 1)
#define DDR_SLICE0 				(1 << 3)
#define DDR_SLICE1 				(1 << 4)
#define DDR_SLICE2 				(1 << 5)
#define DDR_SLICE3 				(1 << 6)
#define DDR_SLICE4 				(1 << 7)
#define DDR_SLICE5 				(1 << 8)
#define DDR_SLICE6 				(1 << 9)
#define DDR_SLICE7 				(1 << 10)
#define DDR_READ				(1 << 15)
#define DDR_WRITE				(1 << 16)
#define DDR_WRITE_READ			(1 << 17)
#define DDR_PLUS_VAL			(1 << 18)
#define DDR_MINUS_VAL			(1 << 19)

#define UPDOWN_ADDR_OFFSET		0x18128
#define SLICE_ADDR_OFFSET		0x1812c
#define MARGIN_CON_ADDR_OFFSET	0x18194

#define TEST_SIZE				0x20000

#define MARGIN_VALUE_MAX	60
char margin_arr[MARGIN_VALUE_MAX] =
{
	0x9A,0x99,0x98,0x97,0x96,0x95,0x94,0x93,0x92,0x91,
	0x90,0x8F,0x8E,0x8D,0x8C,0x8B,0x8A,0x89,0x88,0x87,
	0x86,0x85,0x84,0x83,0x82,0x81,0xC0,0xC1,0xC2,0xC3,
	0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,
	0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,
	0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE0,0xE1
};

unsigned int up_down_addr_phy, up_down_addr_vir;

void ddr_test_updown(unsigned int oper, unsigned int updown, unsigned int *regval)
{
	unsigned int i=0;
	unsigned int find_flag=0;

	if(oper & DDR_READ)
	{
		if(updown == DDR_PLUS_VAL)
		{
			for(i=0; i < MARGIN_VALUE_MAX; i++)
			{
				if(margin_arr[i] == (*regval & 0xFF))
				{
					find_flag =1;
					PRINT_T2D("Current margin value [0x%x]\n", margin_arr[i]);
					PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
					break;
				}
			}

			if((find_flag == 1) && (i < MARGIN_VALUE_MAX-1))
			{
				*regval = margin_arr[i+1];
			}
			else
			{
				PRINT_T2D("Current margin register value [0x%x]\n",(*regval & 0xFF) );
				PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
			}
		}
		else if(updown == DDR_MINUS_VAL)
		{
			for(i=0; i < MARGIN_VALUE_MAX; i++)
			{
				if(margin_arr[i] == (*regval & 0xFF))
				{
					find_flag = 1;
					PRINT_T2D("Current margin value [0x%x]\n", margin_arr[i]);
					PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
					break;
				}
			}

			if((find_flag == 1) && (i != 0))
			{
				*regval = margin_arr[i-1];
			}
			else
			{
				PRINT_T2D("Current margin register value [0x%x]\n",(*regval & 0xFF) );
				PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
			}
		}
	}

	if(oper & DDR_WRITE)
	{
		if(updown == DDR_PLUS_VAL)
		{
			for(i=0; i < MARGIN_VALUE_MAX; i++)
			{
				if(margin_arr[i] == (*regval & 0xFF))
				{
					find_flag =1;
					PRINT_T2D("Current margin value [0x%x]\n", margin_arr[i]);
					PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
					break;
				}
			}

			if((find_flag == 1) && (i < MARGIN_VALUE_MAX-1))
			{
				*regval = margin_arr[i+1];
			}
			else
			{
				PRINT_T2D("Current margin register value [0x%x]\n",(*regval & 0xFF) );
				PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
			}
		}
		else if(updown == DDR_MINUS_VAL)
		{
			for(i=0; i < MARGIN_VALUE_MAX; i++)
			{
				if(margin_arr[i] == (*regval & 0xFF))
				{
					find_flag =1;
					PRINT_T2D("Current margin value [0x%x]\n", margin_arr[i]);
					PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
					break;
				}
			}

			if((find_flag == 1) && (i != 0))
			{
				*regval = margin_arr[i-1];
			}
			else
			{
				PRINT_T2D("Current margin register value  [0x%x]\n",(*regval & 0xFF) );
				PRINT_T2D("0x%08x [0x%08x]\n", up_down_addr_phy, up_down_addr_vir);
			}
		}
	}
}


void ddr_test_print(unsigned int oper, unsigned int *Pregval)
{

	if(oper & DDR_SLICE0)
		PRINT_T2D("Slice 0 Value is ");
	if(oper & DDR_SLICE1)
		PRINT_T2D("Slice 1 Value is ");
	if(oper & DDR_SLICE2)
		PRINT_T2D("Slice 2 Value is ");
	if(oper & DDR_SLICE3)
		PRINT_T2D("Slice 3 Value is ");
	if(oper & DDR_SLICE4)
		PRINT_T2D("Slice 4 Value is ");
	if(oper & DDR_SLICE5)
		PRINT_T2D("Slice 5 Value is ");
	if(oper & DDR_SLICE6)
		PRINT_T2D("Slice 6 Value is ");
	if(oper & DDR_SLICE7)
		PRINT_T2D("Slice 7 Value is ");

	PRINT_T2D("0x%08x\n", (*Pregval)&0xf);
}

int ddr_margin(void)
{
	unsigned int oper, direction, updown;
	unsigned int slice_val = 0;
	unsigned int Preg_slice = 0;
	unsigned int Preg_margin_con = 0;		
	void __iomem *reg_slice;
	void __iomem *reg_margin_con;
	void __iomem *ddr_reg_base;
	phys_addr_t  *test_addr=NULL;
	unsigned int i, count, loop;
	int command;
	sdp_ddrtype_e ddr_type;
	phys_addr_t address;
	uint32_t data[3];
	uint32_t size;
	int ret_val;
	struct device_node *np_mem;
	struct device_node *np_hwmem;
	uint32_t ddr_reg[6];
	int cnt;
	unsigned int reg_margin_con_addr;


	np_mem = of_find_node_by_name(NULL,"sdp_mmap");
	if (np_mem == NULL){
		PRINT_T2D("Could Not find sdp_mmap entry\n");
		return -EINVAL;
	}	

	np_hwmem = of_find_node_by_name(NULL,"sdp_hwmem");
	if (np_hwmem == NULL){
		PRINT_HWMEM_ERR("Could Not find sdp_hwmem entry\n");
		return -EINVAL;

	}
	
	/* Both HawkM and HawkP (including hawkUS) have three DDR's*/
	ret_val = of_property_read_u32_array(np_hwmem, "reg", ddr_reg, 6);
	if (ret_val)
	{
		PRINT_T2D("failed to parse hw_mem reg address and size for DDR margin test\n");
		return -EINVAL;
	}

	PRINT_T2D("Select DDR to be tested \n");
	PRINT_T2D(" 1--> DDR_A\n");
	PRINT_T2D(" 2--> DDR_B\n");
	PRINT_T2D(" 4--> DDR_C/DDR_US\n");
	PRINT_T2D(" ==>");
	ddr_type = t2d_dbg_get_event_as_numeric(NULL, NULL);
	PRINT_T2D("\n");
	oper = ddr_type;

	switch(ddr_type)
	{
		case SDP_DDR_CHECK_0:
			ret_val = of_property_read_u32_array(np_mem, "samsung,pvr", data, 3);
			if (ret_val)
			{
				PRINT_T2D("failed to parse samsung,pvr memory\n");
				return -EINVAL;
			}
			
			address = (phys_addr_t)((uint64_t)data[0] << 32) | data[1];
			size = data[2];
			test_addr = (phys_addr_t*)ioremap_nocache(address, size);
			if(!test_addr)
			{
				PRINT_T2D("Failed to get DDR0 memory for DDR check\n");
				return -ENOMEM;
			}

			ddr_reg_base = ioremap_nocache(ddr_reg[0], ddr_reg[1]);
			if(!ddr_reg_base)
			{
				PRINT_HWMEM_ERR("Failed to get DDR0 register address for DDR Margin check\n");
				return -ENOMEM;
			}

			up_down_addr_phy = ddr_reg[0] + UPDOWN_ADDR_OFFSET;
			cnt = 4;
			break;

		case SDP_DDR_CHECK_1:
#if defined(CONFIG_ARCH_SDP1406)
			if (soc_is_sdp1406fhd())
			{
				ret_val = of_property_read_u32_array(np_mem, "samsung,align0", data, 3); 
				cnt = 6;
			}
			else
			{
				if (of_machine_is_compatible("samsung,sdp1406_2_5"))
					ret_val = of_property_read_u32_array(np_mem, "samsung,dp-vt-c", data, 3); 
				else
					ret_val = of_property_read_u32_array(np_mem, "samsung,align3", data, 3);
				cnt = 8;
			}
#else // SDP1404
			ret_val = of_property_read_u32_array(np_mem, "samsung,dp-vt-c", data, 3);
			cnt = 8;
#endif
			if (ret_val)
			{
				PRINT_T2D("failed to parse samsung,pvr memory\n");
				//return ret;
			}
			
			address = (phys_addr_t)((uint64_t)data[0] << 32) | data[1];
			size = data[2];
			test_addr = (phys_addr_t*)ioremap_nocache(address, size);
			if(!test_addr)
			{
				PRINT_T2D("Failed to get DDR0 memory for DDR check\n");
				return -ENOMEM;
			}

			ddr_reg_base = ioremap_nocache(ddr_reg[2], ddr_reg[3]);
			if(!ddr_reg_base)
			{
				PRINT_HWMEM_ERR("Failed to get DDR0 register address for DDR Margin check\n");
				return -ENOMEM;
			}

			up_down_addr_phy = ddr_reg[2] + UPDOWN_ADDR_OFFSET;
			break;

		case SDP_DDR_CHECK_2:
#ifdef CONFIG_ARCH_SDP1406
			if (soc_is_sdp1406fhd())
			{
				PRINT_T2D("ERROR: DDR MARGIN TEST: DDR_C is not present in HAWKM_FHD, hence cannot test it\n");
				return -ENOMEM;
			}
			else // UHD
			{
				ret_val = of_property_read_u32_array(np_mem, "samsung,dp2", data, 3);
			}
#else // SDP1404
			// HawkUS case
			ret_val = of_property_read_u32_array(np_mem, "samsung,sfa-test_base", data, 3);
#endif
			if (ret_val)
			{
				PRINT_T2D("failed to parse samsung,pvr memory\n");
				//return ret;
			}
	
			address = (phys_addr_t)((uint64_t)data[0] << 32) | data[1];
			size = data[2];
			test_addr = (phys_addr_t*)ioremap_nocache(address, size);
			if(!test_addr)
			{
				PRINT_T2D("Failed to get DDR memory for DDR check\n");
				return -ENOMEM;
			}

			ddr_reg_base = ioremap_nocache(ddr_reg[4], ddr_reg[5]);
			if(!ddr_reg_base)
			{
				PRINT_HWMEM_ERR("Failed to get DDR register address for DDR Margin check\n");
				return -ENOMEM;
			}

			up_down_addr_phy = ddr_reg[4] + UPDOWN_ADDR_OFFSET;
			cnt = 4;
			break;

		default:
			PRINT_T2D("ERROR: wrong DDR option selected\n");
			return -EINVAL;
	}

	up_down_addr_vir = ddr_reg_base + UPDOWN_ADDR_OFFSET;

	PRINT_T2D("Test DDR Virtual Address : 0x%08x\n", (unsigned int)test_addr);
	PRINT_T2D("Select the Slice : \n");
	PRINT_T2D("---------------------------------------\n");
	PRINT_T2D("\t00.Slice 0\n");
	PRINT_T2D("\t01.Slice 1\n");
	PRINT_T2D("\t02.Slice 2\n");
	PRINT_T2D("\t03.Slice 3\n");
	PRINT_T2D("\t04.Slice 4\n");
	PRINT_T2D("\t05.Slice 5\n");
	PRINT_T2D("\t06.Slice 6\n");
	PRINT_T2D("\t07.Slice 7\n");
	PRINT_T2D("---------------------------------------\n");

	command = t2d_dbg_get_event_as_numeric(NULL, NULL);

	switch(command)
	{
		case 0:
			oper |= DDR_SLICE0;
		break;
		case 1:
			oper |= DDR_SLICE1;
		break;
		case 2:
			oper |= DDR_SLICE2;
		break;
		case 3:
			oper |= DDR_SLICE3;
		break;
		case 4:
			oper |= DDR_SLICE4;
		break;
		case 5:
			oper |= DDR_SLICE5;
		break;
		case 6:
			oper |= DDR_SLICE6;
		break;
		case 7:
			oper |= DDR_SLICE7;
		break;			
		default:
			PRINT_T2D("ERROR: wrong slice option selected\n");
			return -EINVAL;
	}
	
	slice_val = 7 * command;
	
	PRINT_T2D("Select Operation : \n");
	PRINT_T2D("---------------------------------------\n");
	PRINT_T2D("\t01.READ\n");
	PRINT_T2D("\t02.WRITE\n");
	PRINT_T2D("---------------------------------------\n");
	command = t2d_dbg_get_event_as_numeric(NULL, NULL);
	switch(command)
	{
		case 1:
			oper|=DDR_READ;
			direction = DDR_READ;
		break;
		case 2:
			oper|=DDR_WRITE;
			direction = DDR_WRITE;
		break;
		default:
			PRINT_T2D("ERROR: wrong operation selected\n");
			return -EINVAL;
	}
	
	PRINT_T2D("Select Direction : \n");
	PRINT_T2D("---------------------------------------\n");
	PRINT_T2D("\t01.+\n");
	PRINT_T2D("\t02.-\n");
	PRINT_T2D("---------------------------------------\n");
	command = t2d_dbg_get_event_as_numeric(NULL, NULL);
	switch(command)
	{
		case 1:
			oper|= DDR_PLUS_VAL;
			updown = DDR_PLUS_VAL;
		break;
		case 2:
			oper|= DDR_MINUS_VAL;
			updown = DDR_MINUS_VAL;
		break;
		default:
			PRINT_T2D("ERROR: wrong direction selected\n");
			return -EINVAL;
	}
	
	PRINT_T2D("Access Count : ");
	count = t2d_dbg_get_event_as_numeric(NULL, NULL);
	if(count > 65535)
	{
		count = 65535;
		PRINT_T2D("over 65536... count = 65535\n");
	}

	reg_slice = ddr_reg_base + SLICE_ADDR_OFFSET;
	reg_margin_con = ddr_reg_base + MARGIN_CON_ADDR_OFFSET; 

	if(oper & DDR_WRITE)
	{
		reg_margin_con += 12;	//write register address = read register address + 12	, read => 0x1884_8194, write = 0x1844_81a0
	}

	reg_margin_con_addr = (direction == DDR_READ) ? (address + MARGIN_CON_ADDR_OFFSET) : (address + MARGIN_CON_ADDR_OFFSET + 0xC);
	for (cnt=0; cnt<4; cnt++)
	{
		writel(cnt*7, reg_slice);
		PRINT_T2D("ch(%d), slice %d current center value at (0x%08x) : 0x%08x\n", ddr_type, cnt, reg_margin_con_addr, (unsigned int)(reg_margin_con));
	}

	if(direction == DDR_READ)
	{
		unsigned int *test_area;
		unsigned int maxcnt;
		unsigned int cnt = 0;

		unsigned int new_margin;
		PRINT_T2D("Start margin value :\n");
		new_margin = t2d_dbg_get_event_as_numeric(NULL, NULL);
		Preg_margin_con = new_margin;

		PRINT_T2D("Margin Test Count (0 ~ 65536) ?\n");
		maxcnt = t2d_dbg_get_event_as_numeric(NULL, NULL);
		
		while(1)
		{
			if(cnt > maxcnt)
				break;

			cnt++;

			test_area=(unsigned int *)(unsigned int)test_addr;
	
			for(i = 0; i < TEST_SIZE/4; i++)
			{
				test_area[i] = 0x5aa55aa5;
			}
			
			Preg_slice = slice_val;	

			ddr_test_updown(oper, updown, &Preg_margin_con);
			ddr_test_print(oper, &Preg_slice);

			for(loop = 0; loop < 8; loop++)
			{ 
				Preg_slice = (Preg_slice & ~0x700) + (loop << 8);
				writel(Preg_slice, reg_slice);
				writel(Preg_margin_con, reg_margin_con);
				PRINT_T2D("0x%x = 0x%x  , 0x%x = 0x%x  \n", (unsigned int)(address + SLICE_ADDR_OFFSET), Preg_slice, (unsigned int)reg_margin_con_addr, Preg_margin_con);
		 	}

			for(loop = 0; loop < count; loop++)
			{
				test_area=(unsigned int *)(unsigned int)test_addr;			
				for(i = 0; i < TEST_SIZE/4; i++)
				{
					if(0x5aa55aa5 != test_area[i])
					{
						PRINT_T2D("Error : DDR Address = 0x%08x, value = 0x%8x\n", (unsigned int)(test_addr+(i*4)), (unsigned int)test_area[i]);
						return 0;
					}
				}
				PRINT_T2D("Read is OK...\n");
			}

			msleep(5);
		}
	}

	if(direction == DDR_WRITE)
	{
		unsigned int *test_area;
		unsigned int maxcnt;
		unsigned int cnt = 0;
		unsigned int new_margin;

		PRINT_T2D("Start margin value :\n");
		new_margin = t2d_dbg_get_event_as_numeric(NULL, NULL);
		Preg_margin_con = new_margin;

		PRINT_T2D("Margin Test Count (0 ~ 65536) ?\n");
		maxcnt = t2d_dbg_get_event_as_numeric(NULL, NULL);

		while(1)
		{
			if(cnt > maxcnt)
				break;

			cnt++;

			memset((void *)test_addr,0,TEST_SIZE);

			Preg_slice = slice_val;

			ddr_test_updown(oper, updown, &Preg_margin_con);
			ddr_test_print(oper, &Preg_slice);

			for(loop = 0; loop < 8; loop++)
			{
				Preg_slice = (Preg_slice & ~0x700) + (loop << 8);
				writel(Preg_slice, reg_slice);
				writel(Preg_margin_con, reg_margin_con);
				PRINT_T2D("0x%x = 0x%x  , 0x%x = 0x%x  \n", (unsigned int)(address + SLICE_ADDR_OFFSET), Preg_slice, (unsigned int)reg_margin_con_addr, Preg_margin_con);
		 	}

			for(loop = 0 ; loop < count ; loop++)
			{
				test_area=(unsigned int *)(unsigned int)test_addr;
				for(i = 0; i < TEST_SIZE/4; i++)
				{
					test_area[i] = 0x5aa55aa5;
				}

				for(i = 0; i < TEST_SIZE/4; i++)
				{
					if(0x5aa55aa5 != test_area[i])
					{
						PRINT_T2D("Compare Error : DDR Address = 0x%08x, value = 0x%8x\n", (unsigned int)(test_addr+(i*4)), (unsigned int)test_area[i]);
						return 0;
					}
				}

				PRINT_T2D("Write is OK...\n");
			}

			msleep(5);
		}
	}
	
	PRINT_T2D("Waiting for writing...\n");
	memset((void *)test_addr,0,TEST_SIZE);

	for(i = 0; i < TEST_SIZE/4; i++) 
		*(test_addr+(i*4)) = 1;

	PRINT_T2D("Write is OK...\n");
	PRINT_T2D("Reading and then Comparing...\n");
	
	for(i = 0; i < TEST_SIZE/4; i++)
	{
		if(1 != *(test_addr + (i*4)))
		{
			PRINT_T2D("Error : DDR Address = 0x%08x,i=%d, value = 0x%08x, i=%d\n", (unsigned int)(test_addr+(i*4)),i, (unsigned int)(*(test_addr+(i*4))), i);
			return 0;
		}
	}
	PRINT_T2D("Read Ok...\n");
	
	return 0;
	
}

int t2ddebug_ddr_margin_check(void)
{
	long event;
	int flag = 1;

	while(flag)
	{
        PRINT_T2D("\n");
        PRINT_T2D(" 1--> DDR Margin Test\n");
        PRINT_T2D(" 99--> Exit\n");
        PRINT_T2D(" Enter Your choice\n");
        PRINT_T2D(" => ");

        event = t2d_dbg_get_event_as_numeric(NULL, NULL);
        PRINT_T2D("\n");

        switch (event)
        {
	        case 1:
				ddr_margin();
				break;

	        case 99:
                flag = 0;
		        break;

	        default:
                PRINT_T2D("Enter valid choice");
        		break;
        }
	}
	return 1;
}
#endif


static int
sdp_mem_mmap(struct file * file, struct vm_area_struct * vma)
{
	size_t size = vma->vm_end - vma->vm_start;

	if (memblock_intersects(&memblock.memory, (phys_addr_t) (vma->vm_pgoff) << PAGE_SHIFT , (phys_addr_t) size))
		return -EFAULT;
	
	if (file->f_flags & O_SYNC)
		vma->vm_page_prot = __pgprot_modify(vma->vm_page_prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED | L_PTE_XN);

	vma->vm_ops = &mmap_mem_ops;

   /* Remap-pfn-range will mark the range VM_IO and VM_RESERVED */
	if (remap_pfn_range(vma,
    					vma->vm_start,
                        vma->vm_pgoff,
                        size,
                        vma->vm_page_prot))
                return -EAGAIN;
    return 0;
}

static int sdp_mem_open(struct inode *inode, struct file *file)
{
	DPRINT_SDP_HWMEM("open\n");

	return 0;
}


static int sdp_mem_release (struct inode *inode, struct file *file)
{
	DPRINT_SDP_HWMEM("release\n");

	return 0;
}

static const struct file_operations sdp_mem_fops = {
	.owner = THIS_MODULE,
	.open  = sdp_mem_open,
	.release = sdp_mem_release,
	.mmap = sdp_mem_mmap,
};

static struct miscdevice sdp_mem_dev = {
	.minor = SDP_MEM_MINOR,
	.name = DRV_MEM_NAME,
	.fops = &sdp_mem_fops	
};

static int __init sdp_mem_init(void)
{
	int ret_val = 0;

	ret_val = misc_register(&sdp_mem_dev);

	if(ret_val){
		printk(KERN_ERR "[ERR]%s: misc register failed\n", DRV_MEM_NAME);
	}
	else {
		printk(KERN_INFO"[SDP_MEM] %s initialized\n", DRV_MEM_NAME);
	}

	return ret_val;
}

static void __exit sdp_mem_exit(void)
{

	misc_deregister(&sdp_mem_dev);

	return;
}


module_init(sdp_hwmem_init);
module_exit(sdp_hwmem_exit);

module_init(sdp_mem_init);
module_exit(sdp_mem_exit);

MODULE_AUTHOR("seungjun.heo@samsung.com");
MODULE_DESCRIPTION("Driver for SDP HW memory allocation");
MODULE_LICENSE("Proprietary");
