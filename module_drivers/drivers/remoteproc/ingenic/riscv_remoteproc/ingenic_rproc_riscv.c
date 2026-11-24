#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "remoteproc_internal.h"

#define CCU_BASE	0xB2200000

#define XBURST2_CCU_CFCR            (0x0fe0)

#define CCU_CCSR                    (0x0 * 4)
#define CCU_CRER                    (0x1 * 4)
#define CCU_FROM_HOST               (0x2 * 4)
#define CCU_TO_HOST                 (0x3 * 4)
#define CCU_TIME_L                  (0x4 * 4)
#define CCU_TIME_H                  (0x5 * 4)
#define CCU_TIME_CMP_L              (0x6 * 4)
#define CCU_TIME_CMP_H              (0x7 * 4)
#define CCU_INTC_MASK_L             (0x8 * 4)
#define CCU_INTC_MASK_H             (0x9 * 4)
#define CCU_INTC_PEND_L             (0xa * 4)
#define CCU_INTC_PEND_H             (0xb * 4)

#define CCU_PMA_ADR0                (0x10 * 4)
#define CCU_PMA_ADR1                (0x11 * 4)
#define CCU_PMA_ADR2                (0x12 * 4)
#define CCU_PMA_ADR3                (0x13 * 4)
#ifdef CONFIG_SOC_X2600
#define CCU_PMA_ADR4                (0x14 * 4)
#define CCU_PMA_ADR5                (0x15 * 4)
#define CCU_PMA_ADR6                (0x16 * 4)
#define CCU_PMA_ADR7                (0x17 * 4)
#endif
#define CCU_PMA_CFG0                (0x18 * 4)
#define CCU_PMA_CFG1                (0x19 * 4)
#define CCU_PMA_CFG2                (0x1a * 4)
#define CCU_PMA_CFG3                (0x1b * 4)
#ifdef CONFIG_SOC_X2600
#define CCU_PMA_CFG4                (0x1c * 4)
#define CCU_PMA_CFG5                (0x1d * 4)
#define CCU_PMA_CFG6                (0x1e * 4)
#define CCU_PMA_CFG7                (0x1f * 4)
#endif



#define CCU_MASG_REG	(0x2 * 4)
#define CCU_RECV_REG	(0x3 * 4)

#define TX_CH0_MASK	(1 << 31)
#define RX_CH0_MASK	(1 << 15)

#define REMOTE_REC_OK	0x1231

#define MAX_VQRING_NUM	2
#define MAX_RMEM_REGIION_NUM	3

#define VQRING_TX_INDEX	0
#define VQRING_RX_INDEX	1

#define KICK_REMOTE_TX	0xAA
#define KICK_REMOTE_RX	0x55

#define RISCV_RUN	0x0
#define RISCV_STATE_MASK	0x80000000

struct ingenic_riscv_rproc {
	struct reset_control *rst;
	struct rproc *rproc;
	int irq;
	int vqid;
	void __iomem *ccu_base;
	void __iomem *riscv_base;
	unsigned int load_addr;
	struct device *dev;
	struct resource *res;
	struct completion remote_msg_done;
	struct work_struct mbx_work;
	struct reserved_mem rmem[MAX_RMEM_REGIION_NUM];
	int rmem_count;
};

struct of_phandle_iterator {
	/* Common iterator information */
	const char *cells_name;
	int cell_count;
	const struct device_node *parent;

	/* List size information */
	const __be32 *list_end;
	const __be32 *phandle_end;

	/* Current position state */
	const __be32 *cur;
	uint32_t cur_count;
	phandle phandle;
	struct device_node *node;
};

int of_phandle_iterator_init(struct of_phandle_iterator *it,
		const struct device_node *np,
		const char *list_name,
		const char *cells_name,
		int cell_count)
{
	const __be32 *list;
	int size;

	memset(it, 0, sizeof(*it));

	/*
	 *        * one of cell_count or cells_name must be provided to determine the
	 *             * argument length.
	 *                  */
	if (cell_count < 0 && !cells_name)
		return -EINVAL;

	list = of_get_property(np, list_name, &size);
	if (!list)
		return -ENOENT;

	it->cells_name = cells_name;
	it->cell_count = cell_count;
	it->parent = np;
	it->list_end = list + size / sizeof(*list);
	it->phandle_end = list;
	it->cur = list;

	return 0;
}
EXPORT_SYMBOL_GPL(of_phandle_iterator_init);

int of_phandle_iterator_next(struct of_phandle_iterator *it)
{
	uint32_t count = 0;

	if (it->node) {
		of_node_put(it->node);
		it->node = NULL;
	}

	if (!it->cur || it->phandle_end >= it->list_end)
		return -ENOENT;

	it->cur = it->phandle_end;

	/* If phandle is 0, then it is an empty entry with no arguments. */
	it->phandle = be32_to_cpup(it->cur++);

	if (it->phandle) {

		/*
		 *            * Find the provider node and parse the #*-cells property to
		 *                       * determine the argument length.
		 *                                  */
		it->node = of_find_node_by_phandle(it->phandle);

		if (it->cells_name) {
			if (!it->node) {
				pr_err("%pOF: could not find phandle\n",
						it->parent);
				goto err;
			}

			if (of_property_read_u32(it->node, it->cells_name,
						&count)) {
				/*
				 *                    * If both cell_count and cells_name is given,
				 *                                       * fall back to cell_count in absence
				 *                                                          * of the cells_name property
				 *                                                                             */
				if (it->cell_count >= 0) {
					count = it->cell_count;
				} else {
					pr_err("%pOF: could not get %s for %pOF\n",
							it->parent,
							it->cells_name,
							it->node);
					goto err;
				}
			}
		} else {
			count = it->cell_count;
		}

		/*
		 *            * Make sure that the arguments actually fit in the remaining
		 *                       * property data length
		 *                                  */
		if (it->cur + count > it->list_end) {
			pr_err("%pOF: %s = %d found %d\n",
					it->parent, it->cells_name,
					count, it->cell_count);
			goto err;
		}
	}

	it->phandle_end = it->cur + count;
	it->cur_count = count;

	return 0;

err:
	if (it->node) {
		of_node_put(it->node);
		it->node = NULL;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_phandle_iterator_next);


static struct workqueue_struct *mbx_workqueue;

static void riscv_writel(struct rproc *rproc,unsigned int val,unsigned int offset)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	//printk("%s base = 0x%x ======\n",__func__,jzproc->riscv_base);
	writel(val,jzproc->riscv_base + offset);
}

static unsigned int riscv_readl(struct rproc *rproc,unsigned int offset)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	return readl(jzproc->riscv_base + offset);
}

static void ccu_writel(struct rproc *rproc,unsigned int val,unsigned int offset)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	//printk("%s base = 0x%x ======\n",__func__,jzproc->ccu_base);
	writel(val,jzproc->ccu_base + offset);
}

static unsigned int ccu_readl(struct rproc *rproc,unsigned int offset)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	return readl(jzproc->ccu_base + offset);
}

static irqreturn_t ingenic_rproc_irq_handler(int irq, void *data)
{
	unsigned int tmp = 0;
	struct rproc *rproc = (struct rproc*)data;
	struct ingenic_riscv_rproc *jz_rproc = (struct ingenic_riscv_rproc*)rproc->priv;
	int vqid = 0;
	// process msg
	//disable_irq_nosync(irq);
	tmp = riscv_readl(rproc,CCU_TO_HOST);

	/* For other shared mailbox irq. example: inkjet-mcu driver used */
	if (tmp & 0x80000000)
	  return IRQ_HANDLED;

	// TODO clr irq
	riscv_writel(rproc,0,CCU_TO_HOST);
	vqid = tmp & 0xff;
	if(vqid)
		vqid -= 1;
	jz_rproc->vqid = vqid;
	queue_work(mbx_workqueue,&jz_rproc->mbx_work);

	//enable_irq(irq);
	return IRQ_HANDLED;
}


static void ingenic_rproc_kick(struct rproc *rproc, int vqid)
{
	int ret = 0;
	struct ingenic_riscv_rproc *jz_rproc = (struct ingenic_riscv_rproc*)rproc->priv;
	if(vqid >= MAX_VQRING_NUM){
		printk("warning vqid error ,vqid = %d ,rporc kick failed\n",vqid);
		return;
	}

	riscv_writel(rproc,vqid+1,CCU_FROM_HOST);
}



static int ingenic_rproc_mem_alloc(struct rproc *rproc,
				 struct rproc_mem_entry *mem)
{
	struct device *dev = rproc->dev.parent;
	void *va;

	dev_err(dev, "map memory: %pa+%x\n", &mem->dma, mem->len);
	va = ioremap(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va)) {
		dev_err(dev, "Unable to map memory region: %pa+%x\n",
			&mem->dma, mem->len);
		return -ENOMEM;
	}

	/* Update memory entry va */
	mem->va = va;
	printk("%s : va = 0x%x mem = 0x%x \n ",__func__,va,mem);
	return 0;
}

static int ingenic_rproc_mem_release(struct rproc *rproc,
				   struct rproc_mem_entry *mem)
{
	dev_dbg(rproc->dev.parent, "unmap memory: %pa\n", &mem->dma);
	iounmap(mem->va);

	return 0;
}

// parse reserved mem  && init virtio ring buffer
static int ingenic_rproc_parse_mem_regions(struct ingenic_riscv_rproc *jz_rproc)
{
	struct rproc *rproc = jz_rproc->rproc;
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct reserved_mem *rmem;


	of_phandle_iterator_init(&it,np,"memory-region",NULL,0);
	while(of_phandle_iterator_next(&it) == 0){
		rmem = of_reserved_mem_lookup(it.node);
		if(!rmem){
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}
		memcpy(&jz_rproc->rmem[jz_rproc->rmem_count], rmem, sizeof(struct reserved_mem));
		jz_rproc->rmem[jz_rproc->rmem_count].name = it.node->name;
		jz_rproc->rmem_count++;
	}

	return 0;
}


static int ingenic_rproc_add_mem_regions(struct ingenic_riscv_rproc *jz_rproc)
{
	struct rproc *rproc = jz_rproc->rproc;
	struct device *dev = rproc->dev.parent;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	int index = 0;
	int i = 0;

	for(i = 0; i < jz_rproc->rmem_count; i++)
	{
		rmem = &jz_rproc->rmem[i];
		if (strcmp(rmem->name, "vdev0buffer")) {
			// vring buffer
			mem = rproc_mem_entry_init(dev, NULL,
						   (dma_addr_t)rmem->base,
						   rmem->size, rmem->base,
						   ingenic_rproc_mem_alloc,
						   ingenic_rproc_mem_release,
						   rmem->name);

			if (mem)
				rproc_coredump_add_segment(rproc, rmem->base,
							   rmem->size);
		} else {
			// for vdev0buffer
			mem = rproc_of_resm_mem_entry_init(dev, index,
							   rmem->size,
							   rmem->base,
							   rmem->name);
		}
		printk("%s : rmem-base = 0x%x rmem->size = %d va = 0x%x \r\n",__func__,rmem->base,rmem->size,mem->va);
		if (!mem)
			return -ENOMEM;
		rproc_add_carveout(rproc, mem);
		index++;
	}
	// add risv0vring0 for TX,riscv-core SRAM
	//
	return 0;

}

static int ingenic_rproc_start_x2500(struct rproc *rproc)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	unsigned int val = 0;
	unsigned int reset_entry = jzproc->load_addr + 0x80;


	val = riscv_readl(rproc,CCU_CCSR);
	val &= ~(1 << 1);
	riscv_writel(rproc,val, CCU_CCSR);

	/*XBURST2_LEP_STOP*/
	val = ccu_readl(rproc,XBURST2_CCU_CFCR);
	val |= (1 << 31);
	ccu_writel(rproc,val, XBURST2_CCU_CFCR);

	// Reset value
	riscv_writel(rproc,0x43, CCU_CCSR);
	riscv_writel(rproc,reset_entry, CCU_CRER);
	riscv_writel(rproc,0, CCU_FROM_HOST);
	riscv_writel(rproc,0, CCU_TO_HOST     );
	riscv_writel(rproc,0, CCU_INTC_MASK_L );
	riscv_writel(rproc,0, CCU_INTC_MASK_H );
	riscv_writel(rproc,0, CCU_TIME_L      );
	riscv_writel(rproc,0, CCU_TIME_H      );
	riscv_writel(rproc,-1, CCU_TIME_CMP_L  );
	riscv_writel(rproc,-1, CCU_TIME_CMP_H  );
	riscv_writel(rproc,0x1e01, CCU_PMA_CFG0    );
	riscv_writel(rproc,0x2F03, CCU_PMA_CFG1    );
	riscv_writel(rproc,0x1f01, CCU_PMA_CFG2    );
	riscv_writel(rproc,0x1e01, CCU_PMA_CFG3    );
	riscv_writel(rproc,0x04000000, CCU_PMA_ADR0    );
	riscv_writel(rproc,0x05ffffff, CCU_PMA_ADR1    );
	riscv_writel(rproc,0x1fffffff, CCU_PMA_ADR2    );
	riscv_writel(rproc,0x00000000, CCU_PMA_ADR3    );
	/*XBURST2_LEP_RUN*/
	val = ccu_readl(rproc,XBURST2_CCU_CFCR);
	val &= ~(1 << 31);
	ccu_writel(rproc,val, XBURST2_CCU_CFCR);
	return 0;
}

static int ingenic_rproc_start_x2600(struct rproc *rproc)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	unsigned int val = 0;
	unsigned int reset_entry = jzproc->load_addr + 0x80;

	val = riscv_readl(rproc,CCU_CCSR);
	val &= ~(1 << 1);
	riscv_writel(rproc,val, CCU_CCSR);

	/*XBURST2_LEP_STOP*/
	val = ccu_readl(rproc,XBURST2_CCU_CFCR);
	val |= (1 << 31);
	ccu_writel(rproc,val, XBURST2_CCU_CFCR);

	// Reset value
	riscv_writel(rproc,0x23, CCU_CCSR);
	riscv_writel(rproc,reset_entry, CCU_CRER);
	riscv_writel(rproc,0, CCU_FROM_HOST);
	riscv_writel(rproc,0, CCU_TO_HOST     );
	riscv_writel(rproc,0, CCU_INTC_MASK_L );
	riscv_writel(rproc,0, CCU_INTC_MASK_H );
	riscv_writel(rproc,0, CCU_TIME_L      );
	riscv_writel(rproc,0, CCU_TIME_H      );
	riscv_writel(rproc,-1, CCU_TIME_CMP_L  );
	riscv_writel(rproc,-1, CCU_TIME_CMP_H  );
	riscv_writel(rproc,0x7, CCU_PMA_CFG0    );
	riscv_writel(rproc,0x7, CCU_PMA_CFG1    );
	riscv_writel(rproc,0x7, CCU_PMA_CFG2    );
	riscv_writel(rproc,0x7, CCU_PMA_CFG3    );
	riscv_writel(rproc,0x7, CCU_PMA_CFG4    );
	riscv_writel(rproc,0x7, CCU_PMA_CFG5    );
	riscv_writel(rproc,0x1f, CCU_PMA_CFG6    );
	riscv_writel(rproc,0x3f, CCU_PMA_CFG7    );
	riscv_writel(rproc,0x0, CCU_PMA_ADR0    );
	riscv_writel(rproc,0x0, CCU_PMA_ADR1    );
	riscv_writel(rproc,0x0, CCU_PMA_ADR2    );
	riscv_writel(rproc,0x0, CCU_PMA_ADR3    );
	riscv_writel(rproc,0x0, CCU_PMA_ADR4    );
	riscv_writel(rproc,0x0, CCU_PMA_ADR5    );
	riscv_writel(rproc,0x05ffffff, CCU_PMA_ADR6    );
	riscv_writel(rproc,0x0fffffff, CCU_PMA_ADR7    );
	/*XBURST2_LEP_RUN*/
	val = ccu_readl(rproc,XBURST2_CCU_CFCR);
	val &= ~(1 << 31);
	ccu_writel(rproc,val, XBURST2_CCU_CFCR);

	return 0;
}

static int ingenic_rproc_start(struct rproc *rproc)
{
#ifdef CONFIG_SOC_X2500
	return ingenic_rproc_start_x2500(rproc);
#elif CONFIG_SOC_X2600
	return ingenic_rproc_start_x2600(rproc);
#endif
}

static int ingenic_rproc_stop(struct rproc *rproc)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	unsigned int val = 0;

	/*XBURST2_LEP_STOP*/
	val = ccu_readl(rproc,XBURST2_CCU_CFCR);
	val |= (1 << 31);
	ccu_writel(rproc,val, XBURST2_CCU_CFCR);

	return 0;
}

static int ingenic_rproc_attach(struct rproc *rproc)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	return 0;
}

static void* ingenic_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len)
{
	void* vaddr = NULL;
	if(da > 0 && da < 0x10000000)
		vaddr = da | 0xa0000000;
	else
		vaddr = phys_to_virt(da);
	return vaddr;
}

static unsigned int ingenic_get_riscv_state(struct ingenic_riscv_rproc *jz_rproc)
{
	return readl(jz_rproc->ccu_base + 0xFE0);
}

static int ingenic_rproc_load_bin(struct rproc *rproc,const struct firmware *fw)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	unsigned int load_vaddr = phys_to_virt(jzproc->load_addr);

	const unsigned char* bin_data = fw->data;
	if(fw->size){
		printk(" %s firmware size = %d load_vaddr = 0x%x #########\n",__func__,fw->size,load_vaddr);
		memcpy(load_vaddr,bin_data,fw->size);
		//dma_cache_wback(jzproc->load_addr,100 * 1024);
	}
	printk("%s end !!!!\n",__func__);
	return 0;
}

static void ingenic_riscvmbx_worker(struct work_struct *work)
{
	struct ingenic_riscv_rproc *jzrproc = container_of(work, struct ingenic_riscv_rproc,mbx_work);
	struct rproc *rproc = jzrproc->rproc;
	if (rproc_vq_interrupt(rproc, jzrproc->vqid) == IRQ_NONE){
		dev_err(&rproc->dev,"no message find in qv %d \n",jzrproc->vqid);
	}


}

static int ingenic_rproc_parse_fw(struct rproc *rproc,const struct firmware *fw)
{
	struct ingenic_riscv_rproc *jzproc = (struct ingenic_riscv_rproc*)rproc->priv;
	ingenic_rproc_add_mem_regions(jzproc);
	if (rproc_elf_load_rsc_table(rproc, fw))
		dev_warn(&rproc->dev, "no resource table found for this firmware\n");
	return 0;
}

static struct rproc_ops ingenic_rproc_ops = {
	.start   = ingenic_rproc_start,
	.stop    = ingenic_rproc_stop,
	.attach  = ingenic_rproc_attach,
	.kick    = ingenic_rproc_kick,
	.da_to_va = ingenic_rproc_da_to_va,
	//.load	 = ingenic_rproc_load_bin,
	.load		= rproc_elf_load_segments,
	.parse_fw = ingenic_rproc_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
};



static int ingenic_rproc_riscv_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rproc *rproc;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ingenic_riscv_rproc *jz_rproc;
	unsigned int state = 0;
	rproc = rproc_alloc(dev,np->name,&ingenic_rproc_ops,NULL,sizeof(struct ingenic_riscv_rproc));
	if(!rproc){
		dev_err(&pdev->dev,"alloc rproc failed !!\n");
		return -ENOMEM;
	}

	jz_rproc = rproc->priv;

	// test
	jz_rproc->rproc = rproc;

	init_completion(&jz_rproc->remote_msg_done);
	jz_rproc->res = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(!jz_rproc->res){
		dev_err(&pdev->dev,"get dev resource failed\n");
		ret = -EINVAL;
		goto err_get_platform_res;
	}
	jz_rproc->res = request_mem_region(jz_rproc->res->start,
			jz_rproc->res->end - jz_rproc->res->start + 1,
			pdev->name);
	if(!jz_rproc->res){
		dev_err(&pdev->dev,"request regs memory failed\n");
		ret = -EINVAL;
		goto err_get_mem_region;
	}

	jz_rproc->riscv_base = ioremap(jz_rproc->res->start,resource_size(jz_rproc->res));
	if(!jz_rproc->riscv_base){
		dev_err(&pdev->dev,"remap regbase failed \n");
		ret = -EINVAL;
		goto err_ioremap;
	}

	if(of_property_read_u32(np,"load-addr",&jz_rproc->load_addr)){
		dev_err(&pdev->dev,"get loadaddr failed \n");
		ret = -EINVAL;
		goto err_get_loadaddr;
	}

	jz_rproc->ccu_base = CCU_BASE;

	jz_rproc->irq = platform_get_irq(pdev,0);
	ret = request_irq(jz_rproc->irq, ingenic_rproc_irq_handler,
			  IRQF_ONESHOT | IRQF_SHARED,rproc->name,rproc);
	if(ret){
		dev_err(&pdev->dev,"request irq failed\n");
		ret = -EINVAL;
		goto err_request_irq;
	}

	//rproc_coredump_set_elf_info(rproc, ELFCLASS32,25);

	// todo add reserved mem init,
	// riscv boot addr prased from dts
	//ingenic_jz_rproc_prase_dt(pdev,jz_rproc);

	ingenic_rproc_parse_mem_regions(jz_rproc);

	state = ingenic_get_riscv_state(jz_rproc);
	if(state & RISCV_STATE_MASK == RISCV_RUN){
		// uboot has runing the riscv-core
		ingenic_rproc_add_mem_regions(jz_rproc);
		// TODO get elf info
	} else {

	}

	mbx_workqueue = create_workqueue(dev_name(dev));
	if(!mbx_workqueue){
		dev_err(&pdev->dev,"create_workqueue failed\n");
		ret = -EINVAL;
		goto err_request_irq;
	}
	INIT_WORK(&jz_rproc->mbx_work,ingenic_riscvmbx_worker);
	platform_set_drvdata(pdev,rproc);

	ret = rproc_add(rproc);
	if(ret)
		goto err_add_rproc;

	printk("[rproc  %d ] %s end !!!!\n",__LINE__,__func__);
	return 0;
err_add_rproc:
err_request_irq:
err_get_loadaddr:
	iounmap(jz_rproc->ccu_base);
err_ioremap:
	release_mem_region(jz_rproc->res->start,jz_rproc->res->end - jz_rproc->res->start + 1);
err_get_mem_region:
err_get_platform_res:
	rproc_put(rproc);
	return ret;
}

static const struct of_device_id ingenic_rproc_riscv_of_matches[] = {
	{ .compatible = "ingenic,x2500-riscv-rproc", },
	{ .compatible = "ingenic,x2600-riscv-rproc", },
	{}
};
MODULE_DEVICE_TABLE(of, ingenic_rproc_riscv_of_matches);

static struct platform_driver ingenic_rproc_riscv_driver = {
	.probe = ingenic_rproc_riscv_probe,
	.driver = {
		.name = "ingenic-riscv",
		.of_match_table = ingenic_rproc_riscv_of_matches,
	},
};
module_platform_driver(ingenic_rproc_riscv_driver);

MODULE_LICENSE("GPL");
