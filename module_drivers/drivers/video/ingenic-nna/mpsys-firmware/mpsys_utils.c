#include <linux/list.h>

#include "mpsys-funcs.h"

#define SOC_NNA_MAGIC 'c'
#define NMEM_INIT         _IOWR(SOC_NNA_MAGIC, 7, int)
#define NNA_LOCK          _IOWR(SOC_NNA_MAGIC, 8, int)
#define NNA_UNLOCK        _IOWR(SOC_NNA_MAGIC, 9, int)
#define NMEM_UNINIT   _IOWR(SOC_NNA_MAGIC, 10, int)

typedef enum status
{
        INUSE,
        UNUSE,
} STATUS;

struct nmem_info
{
        struct list_head list;
        int size;
        int paddr;
        STATUS stat;
        pid_t tgid;
};

DEFINE_SEMAPHORE(sema_lock);
DEFINE_MUTEX(mutex_nmem);
static LIST_HEAD(nmem_list);

/* 记录nmem的大小和起始地址 */
static int nsize = 0;
static int nstart = 0;

/* 记录nmem当前剩余的内存起始地址和大小 */
static int remain_start = 0;
static int remain_size = 0;
static long release_time_stamp = 0;

/* 从/proc/cmdline中获取nmem的信息 */
static int get_nmem_info(int *nsize, int *nstart)
{
        char *cmdline;
        char nmem_size[8] = {0};
        char nmem_start[16] = {0};
        int size_m = 0;
        char *p = NULL;
        int ret = 0;
        cmdline = mpsys_kzalloc(512, GFP_KERNEL);
        strncpy(cmdline, saved_command_line, 512);

        p = mpsys_strstr(cmdline, "nmem=");
        if (p)
        {
                p += mpsys_strlen("nmem=");
        }

        if (p)
        {
                char *tmp = p;
                char mem_unit = 0;

                if (mpsys_strstr(tmp, "K@"))
                        mem_unit = 'K';
                else if (mpsys_strstr(tmp, "M@"))
                        mem_unit = 'M';

                while (*++tmp != mem_unit)
                        ;
                *tmp = '\0';
                strcpy(nmem_size, p);

                tmp += 2; // M@
                p = tmp;

                while (*++tmp != ' ')
                        ;
                *tmp = '\0';
                strcpy(nmem_start, p);

                printk("nmem_size = %s%c\n", nmem_size, mem_unit);
                printk("nmem_start = %s\n", nmem_start);

                ret = mpsys_kstrtoint(nmem_size, 10, &size_m);
                ret = (!ret) && mpsys_kstrtoint(nmem_start, 16, nstart);
                if (!ret)
                {
                        *nsize = size_m * 1024 * ((mem_unit == 'M')? 1024 : 1); // bytes
                        remain_start = *nstart;
                        remain_size = *nsize;
                }
        }
        else
        {
                ret = -EINVAL;
                printk("can't get nmem info, please check bootargs!\n");
        }
        mpsys_kfree(cmdline);


        return ret;
}

static int nmem_alloc(int alloc_size)
{
        int ret = -ENOMEM;
        struct nmem_info *info = NULL;

        /* 先从链表中寻找有没有已经分配且可用的节点 */
        list_for_each_entry(info, &nmem_list, list)
        {
                if ((info->stat == UNUSE) && (info->size == alloc_size))
                {
                        printk("alloc nmem from list\n");
                        info->stat = INUSE;
                        info->tgid = current->tgid;
                        ret = info->paddr;
                        break;
                }
        }

        if (ret == -ENOMEM)
        {
                /* 从nmem的剩余空间分配 */
                printk("remain size = 0x%x, alloc_size = 0x%x\n", remain_size, alloc_size);
                if (remain_size >= alloc_size)
                {
                        ret = remain_start;
                        remain_start += alloc_size;
                        remain_size -= alloc_size;

                        info = kmalloc(sizeof(struct nmem_info), GFP_KERNEL);
                        // printk("info = %p\n", info);
                        info->paddr = ret;
                        info->size = alloc_size;
                        info->stat = INUSE;
                        info->tgid = current->tgid;
                        list_add_tail(&info->list, &nmem_list);
                }
                else
                {
                        printk("nmem is not enough!\n");
                }
        }
        printk("remain size = 0x%x, alloc_size = 0x%x add = 0x%x\n", remain_size, alloc_size, ret);

        return ret;
}


static int mpsys_utils_open(struct inode *inode, struct file *filp)
{
        long time_diff = 500 - (jiffies_to_msecs(jiffies) - release_time_stamp);

        if (time_diff > 0) {
                mpsys_msleep(time_diff);
        }

        filp->private_data = NULL;
        return 0;
}

// 释放链表节点申请的堆空间
void free_list(void)
{
        struct nmem_info *info = NULL, *info_n;

        list_for_each_entry_safe(info, info_n, &nmem_list, list)
        {
                printk("free list info = %p\n", info);
                list_del(&info->list);
                mpsys_kfree(info);
        }
}

void nmem_dealloc(void)
{
        struct nmem_info *info = NULL;

        list_for_each_entry(info, &nmem_list, list)
        {
                if (info->tgid == (current->tgid) && (info->stat == INUSE))
                {
                        info->stat = UNUSE;
                        info->tgid = 0;
                        break;
                }
        }
}

struct user_nmem
{
        int size;       /* 应用层想要分配的内存大小 */
        int paddr;	/* 返回给应用层的物理地址 */
};

static long mpsys_utils_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        int ret = 0;
        struct user_nmem unmem = {0};

        switch (cmd)
        {
        case NNA_LOCK:
        {
                ret = mpsys_down_interruptible(&sema_lock);
                if (ret == -EINTR)
                {
                        break;
                }

                filp->private_data = (void *)current->tgid;
                // printk("private_data = %p\n", filp->private_data);
                break;
        }

        case NNA_UNLOCK:
        {
                filp->private_data = NULL;
                mpsys_up(&sema_lock);
                break;
        }

        case NMEM_INIT:
        {
                mpsys_mutex_lock(&mutex_nmem);
                mpsys_copy_from_user(&unmem, (void *)arg, sizeof(unmem));
                printk("NMEM_INIT size: 0x%x\n", unmem.size);

                ret = nmem_alloc(unmem.size);
                mpsys_copy_to_user(&(((struct user_nmem *)arg)->paddr), &ret, sizeof(ret));
                mpsys_mutex_unlock(&mutex_nmem);

                break;
        }

        case NMEM_UNINIT:
        {
                mpsys_mutex_lock(&mutex_nmem);
                printk("NMEM_UNINIT\n");

                nmem_dealloc();
                mpsys_mutex_unlock(&mutex_nmem);

                break;
        }

        default:
        {
                break;
        }
        }

        return ret;
}

static int mpsys_utils_release(struct inode *inode, struct file *filp)
{
        // printk("nnalock_release private_data = %p\n", filp->private_data);
        if (filp->private_data)
        {
                mpsys_up(&sema_lock);
        }

        nmem_dealloc();

        /*
         * 此处的500ms延时是为了解决bscaler超时的问题
         * 如果当前进程bscaler硬件正在处理还未产生中断接收到了SIGINT
         * 那么该进程的锁会被下一个进程使用
         * 下一个进程会再次进入bscaler中，此时上一个进程bscaler还在处理中
         * 这样bscaler可能会产生异常

           private_msleep(500);
         */
        release_time_stamp = jiffies_to_msecs(jiffies);
        return 0;
}

static struct file_operations mpsys_utils_fops = {
        .owner = THIS_MODULE,
        .unlocked_ioctl = mpsys_utils_ioctl,
        .open = mpsys_utils_open,
        .release = mpsys_utils_release,
};

static struct miscdevice mpsys_utils_miscdev = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "nna_lock",
        .fops = &mpsys_utils_fops,
};

int mpsys_utils_init(void)
{
        int ret = 0;

        release_time_stamp = jiffies_to_msecs(jiffies) - 500;

        ret = get_nmem_info(&nsize, &nstart);
        if (ret)
        {
                ret = -EINVAL;
        }
        else
        {
                ret = mpsys_misc_register(&mpsys_utils_miscdev);
                if (ret)
                {
                        printk("Can't register misc device mpsys_utils\n");
                }
        }

        return ret;
}

void mpsys_utils_exit(void)
{
        release_time_stamp = 0;
        mpsys_misc_deregister(&mpsys_utils_miscdev);

        if (!list_empty(&nmem_list))
        {
                free_list();
        }
}
