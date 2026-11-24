/*
 * Coherent per-device memory handling.
 * Borrowed from i386
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>

struct dma_coherent_mem {
	void		*virt_base;
	dma_addr_t	device_base;
	unsigned long	pfn_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
	spinlock_t	spinlock;

	/*For Debug.*/
	unsigned long	allocated_size;
	unsigned long	max_allocated_size;
	struct list_head entry;
	struct list_head node_list;
};

#include <linux/debugfs.h>
#include <linux/seq_file.h>

struct alloc_node {
	struct list_head entry;	/*当前节点的list*/
	struct device *dev;		/*当前内存是哪个设备分配的.*/
	unsigned long addr;		/*当前分配的起始地址*/
	unsigned long size;		/*当前分配的大小*/
};


static LIST_HEAD(dma_mem_list);


static int trace_buffer_allocate = 0;
module_param(trace_buffer_allocate, int,  S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(trace_buffer_allocate, "record buffer allocated info for debug");


static int dma_coherent_debug_show(struct seq_file *m, void *unused)
{

	struct alloc_node *n, *t;
	struct dma_coherent_mem *mem, *tmem;

	seq_puts(m, "dma coherent alloc info\n");

	list_for_each_entry_safe(mem, tmem, &dma_mem_list, entry) {
		seq_printf(m, "dma_mm: %p, <virt:%p size: %d pages>\n", mem, mem->virt_base, mem->size);
		seq_printf(m, "max_allocated_size: %ld, current allocated_size: %ld\n",				mem->max_allocated_size, mem->allocated_size);
		list_for_each_entry_safe(n, t, &mem->node_list, entry) {
			seq_printf(m, "\tdev:%s, addr: %08lx, size: %ld\n", dev_name(n->dev), n->addr, n->size);

		}

	}

	return 0;
}

static int dma_coherent_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, dma_coherent_debug_show, NULL);
}

static const struct file_operations dma_coherent_debug_fops = {
	.owner = THIS_MODULE,
	.open = dma_coherent_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *dma_coherent_alloc_dentry;

static int __init dma_coherent_debugfs_init(void)
{
	dma_coherent_alloc_dentry = debugfs_create_file("dma_coherent_mm",
							S_IRUGO, NULL, NULL, &dma_coherent_debug_fops);
	return 0;
}

late_initcall(dma_coherent_debugfs_init);


static int dma_init_coherent_memory(phys_addr_t phys_addr, dma_addr_t device_addr,
				    size_t size, int flags,
				    struct dma_coherent_mem **mem)
{
	struct dma_coherent_mem *dma_mem = NULL;
	void __iomem *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	if ((flags & (DMA_MEMORY_MAP | DMA_MEMORY_IO)) == 0)
		goto out;
	if (!size)
		goto out;

	mem_base = ioremap(phys_addr, size);
	if (!mem_base)
		goto out;

	dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dma_mem)
		goto out;
	dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dma_mem->bitmap)
		goto out;

	dma_mem->virt_base = mem_base;
	dma_mem->device_base = device_addr;
	dma_mem->pfn_base = PFN_DOWN(phys_addr);
	dma_mem->size = pages;
	dma_mem->flags = flags;
	spin_lock_init(&dma_mem->spinlock);

	*mem = dma_mem;

	INIT_LIST_HEAD(&dma_mem->entry);
	INIT_LIST_HEAD(&dma_mem->node_list);
	list_add_tail(&dma_mem->entry, &dma_mem_list);

	if (flags & DMA_MEMORY_MAP)
		return DMA_MEMORY_MAP;

	return DMA_MEMORY_IO;

out:
	kfree(dma_mem);
	if (mem_base)
		iounmap(mem_base);
	return 0;
}

static void dma_release_coherent_memory(struct dma_coherent_mem *mem)
{
	if (!mem)
		return;
	iounmap(mem->virt_base);
	kfree(mem->bitmap);
	kfree(mem);
}

static int dma_assign_coherent_memory(struct device *dev,
				      struct dma_coherent_mem *mem)
{
	if (dev->dma_mem)
		return -EBUSY;

	dev->dma_mem = mem;
	/* FIXME: this routine just ignores DMA_MEMORY_INCLUDES_CHILDREN */

	return 0;
}
int dma_coherent_mem_size(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	if(mem == NULL)
		return 0;
	return mem->size << PAGE_SHIFT;
}

int dma_coherent_mem_available(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	unsigned int pos;		/* scans bitmap by regions of size order */

	int bitmap_size = (mem->size + 31) / (sizeof(unsigned long) * 8);

	unsigned long total = 0;
	if(mem == NULL)
		return 0;
	/*step = 1 << 5 pages. */
	for (pos = 0; pos < bitmap_size; pos++) {
		unsigned long val = mem->bitmap[pos];
		if(val) {
			val = (val & 0x55555555) + ((val >> 1) & 0x55555555);
			val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
			val = (val & 0x0f0f0f0f) + ((val >> 4) & 0x0f0f0f0f);
			val = (val & 0x00ff00ff) + ((val >> 8) & 0x00ff00ff);
			val = (val & 0x0000ffff) + ((val >> 16) & 0x0000ffff);
			total += val;
		}
	}
	total = (mem->size - total) << PAGE_SHIFT;
	return total;
}

int dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
				dma_addr_t device_addr, size_t size, int flags)
{
	struct dma_coherent_mem *mem;
	int ret;

	ret = dma_init_coherent_memory(phys_addr, device_addr, size, flags,
				       &mem);
	if (ret == 0)
		return 0;

	if (dma_assign_coherent_memory(dev, mem) == 0)
		return ret;

	dma_release_coherent_memory(mem);
	return 0;
}
EXPORT_SYMBOL(dma_declare_coherent_memory);

void dma_release_declared_memory(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;

	if (!mem)
		return;
	dma_release_coherent_memory(mem);
	dev->dma_mem = NULL;
}
EXPORT_SYMBOL(dma_release_declared_memory);

void *dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	unsigned long flags;
	int pos, err;

	size += device_addr & ~PAGE_MASK;

	if (!mem)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&mem->spinlock, flags);
	pos = (device_addr - mem->device_base) >> PAGE_SHIFT;
	err = bitmap_allocate_region(mem->bitmap, pos, get_order(size));
	spin_unlock_irqrestore(&mem->spinlock, flags);

	if (err != 0)
		return ERR_PTR(err);
	return mem->virt_base + (pos << PAGE_SHIFT);
}
EXPORT_SYMBOL(dma_mark_declared_memory_occupied);

static int dma_bitmap_find_free_region(unsigned long *bitmap, unsigned int bits, unsigned int pages)
{
	unsigned int pos, end;		/* scans bitmap by regions of size order */
	unsigned long start = 0;

	/*step = 1 << 5 pages. */
	for (pos = 0; (end = pos + (1U << 5)) <= bits; pos = end) {
		start = bitmap_find_next_zero_area(bitmap, bits, pos, pages, 0);
		if( start > bits) {
			return -ENOMEM;
		}

		break;
	}

	//ALLOC buffer.
	bitmap_set(bitmap, start, pages);

	return start;
}

static int dma_bitmap_release_region(unsigned long *bitmap, unsigned int bits, int pages)
{
	bitmap_clear(bitmap, bits, pages);
	return 0;
}


/**
 * dma_alloc_from_coherent() - try to allocate memory from the per-device coherent area
 *
 * @dev:	device from which we allocate memory
 * @size:	size of requested memory area
 * @dma_handle:	This will be filled with the correct dma handle
 * @ret:	This pointer will be filled with the virtual address
 *		to allocated area.
 *
 * This function should be only called from per-arch dma_alloc_coherent()
 * to support allocation from per-device coherent memory pools.
 *
 * Returns 0 if dma_alloc_coherent should continue with allocating from
 * generic memory areas, or !0 if dma_alloc_coherent should return @ret.
 */
int dma_alloc_from_coherent(struct device *dev, ssize_t size,
			    dma_addr_t *dma_handle, void **ret)
{
	struct dma_coherent_mem *mem;
	int order = get_order(size);
	unsigned int pages = ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT;
	unsigned long flags;
	int pageno;

	if (!dev)
		return 0;
	mem = dev->dma_mem;
	if (!mem)
		return 0;

	dev_dbg(dev, "%s, size: %d, mem->size: %d, order: %d\n", __func__, size, mem->size, order);

	*ret = NULL;
	spin_lock_irqsave(&mem->spinlock, flags);

	if (unlikely(size > (mem->size << PAGE_SHIFT)))
		goto err;

	pageno = dma_bitmap_find_free_region(mem->bitmap, mem->size, pages);
	if (unlikely(pageno < 0))
		goto err;

	/*
	 * Memory was found in the per-device area.
	 */
	*dma_handle = mem->device_base + (pageno << PAGE_SHIFT);
	*ret = mem->virt_base + (pageno << PAGE_SHIFT);

	dev_dbg(dev, "%s, allocated@ %p, start: %d, pages: %d\n", __func__, *ret, pageno, pages);


	if(trace_buffer_allocate == 1) {
		struct alloc_node *n = kzalloc(sizeof(struct alloc_node), GFP_KERNEL);
		n->dev = dev;
		n->addr = (unsigned long)*ret;
		n->size = pages;
		INIT_LIST_HEAD(&n->entry);
		list_add_tail(&n->entry, &mem->node_list);
	}

	mem->allocated_size += pages;
	/*
	  记录最大分配过的内存.
	*/
	if(mem->max_allocated_size < mem->allocated_size) {
		mem->max_allocated_size = mem->allocated_size;
	}
	//memset(*ret, 0, size); /*to improve efficiency*/
	spin_unlock_irqrestore(&mem->spinlock, flags);

	return 1;

err:
	spin_unlock_irqrestore(&mem->spinlock, flags);
	/*
	 * In the case where the allocation can not be satisfied from the
	 * per-device area, try to fall back to generic memory if the
	 * constraints allow it.
	 */
	return mem->flags & DMA_MEMORY_EXCLUSIVE;
}
EXPORT_SYMBOL(dma_alloc_from_coherent);

/**
 * dma_release_from_coherent() - try to free the memory allocated from per-device coherent memory pool
 * @dev:	device from which the memory was allocated
 * @size:	the exact size to release.
 * @vaddr:	virtual address of allocated pages
 *
 * This checks whether the memory was allocated from the per-device
 * coherent memory pool and if so, releases that memory.
 *
 * Returns 1 if we correctly released the memory, or 0 if
 * dma_release_coherent() should proceed with releasing memory from
 * generic pools.
 */
int dma_release_from_coherent(struct device *dev, ssize_t size, void *vaddr)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	unsigned int pages = ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT;

	if (mem && vaddr >= mem->virt_base && vaddr <
	    (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;
		unsigned long flags;

		spin_lock_irqsave(&mem->spinlock, flags);
		dma_bitmap_release_region(mem->bitmap, page, pages);
		dev_dbg(dev, "%s, release@ %p, bits: %d start %d, pages: %d\n", __func__, vaddr, mem->size, page, pages);

		mem->allocated_size -= pages;


		if(trace_buffer_allocate == 1) {
			struct alloc_node *n, *t;
			list_for_each_entry_safe(n, t, &mem->node_list, entry) {
				if(n->dev == dev
				   && n->addr == (unsigned long)vaddr
				   && n->size == pages) {
					/*found node*/
					list_del(&n->entry);
					kfree(n);
				}
			}
		}

		spin_unlock_irqrestore(&mem->spinlock, flags);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(dma_release_from_coherent);

/**
 * dma_mmap_from_coherent() - try to mmap the memory allocated from
 * per-device coherent memory pool to userspace
 * @dev:	device from which the memory was allocated
 * @vma:	vm_area for the userspace memory
 * @vaddr:	cpu address returned by dma_alloc_from_coherent
 * @size:	size of the memory buffer allocated by dma_alloc_from_coherent
 * @ret:	result from remap_pfn_range()
 *
 * This checks whether the memory was allocated from the per-device
 * coherent memory pool and if so, maps that memory to the provided vma.
 *
 * Returns 1 if we correctly mapped the memory, or 0 if the caller should
 * proceed with mapping memory from generic pools.
 */
int dma_mmap_from_coherent(struct device *dev, struct vm_area_struct *vma,
			   void *vaddr, size_t size, int *ret)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;

	if (mem && vaddr >= mem->virt_base && vaddr + size <=
	    (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		unsigned long off = vma->vm_pgoff;
		int start = (vaddr - mem->virt_base) >> PAGE_SHIFT;
		int user_count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
		int count = size >> PAGE_SHIFT;

		*ret = -ENXIO;
		if (off < count && user_count <= count - off) {
			unsigned long pfn = mem->pfn_base + start + off;
			*ret = remap_pfn_range(vma, vma->vm_start, pfn,
					       user_count << PAGE_SHIFT,
					       vma->vm_page_prot);
		}
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(dma_mmap_from_coherent);

/*
 * Support for reserved memory regions defined in device tree
 */
#ifdef CONFIG_OF_RESERVED_MEM
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

static int rmem_dma_device_init(struct reserved_mem *rmem, struct device *dev)
{
	struct dma_coherent_mem *mem = rmem->priv;

	if (!mem &&
	    dma_init_coherent_memory(rmem->base, rmem->base, rmem->size,
				     DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE,
				     &mem) != DMA_MEMORY_MAP) {
		pr_err("Reserved memory: failed to init DMA memory pool at %pa, size %ld MiB\n",
		       &rmem->base, (unsigned long)rmem->size / SZ_1M);
		return -ENODEV;
	}
	rmem->priv = mem;
	dma_assign_coherent_memory(dev, mem);
	return 0;
}

static void rmem_dma_device_release(struct reserved_mem *rmem,
				    struct device *dev)
{
	dev->dma_mem = NULL;
}

static const struct reserved_mem_ops rmem_dma_ops = {
	.device_init	= rmem_dma_device_init,
	.device_release	= rmem_dma_device_release,
};

static int __init rmem_dma_setup(struct reserved_mem *rmem)
{
	unsigned long node = rmem->fdt_node;

	if (of_get_flat_dt_prop(node, "reusable", NULL))
		return -EINVAL;

#ifdef CONFIG_ARM
	if (!of_get_flat_dt_prop(node, "no-map", NULL)) {
		pr_err("Reserved memory: regions without no-map are not yet supported\n");
		return -EINVAL;
	}
#endif

	rmem->ops = &rmem_dma_ops;
	pr_info("Reserved memory: created DMA memory pool at %pa, size %ld MiB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);
	return 0;
}
RESERVEDMEM_OF_DECLARE(dma, "shared-dma-pool", rmem_dma_setup);
#endif
