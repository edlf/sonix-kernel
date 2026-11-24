#include <linux/list.h>

#include "mpsys-funcs.h"

#define MPSYS_DATA_MAGIC 'D'
#define MPSYS_SET_PRODUCER        _IOWR(MPSYS_DATA_MAGIC, 1, int)
#define MPSYS_SET_CONSUMER        _IOWR(MPSYS_DATA_MAGIC, 2, int)
#define MPSYS_GET_DATA            _IOWR(MPSYS_DATA_MAGIC, 3, int)
#define MPSYS_PUSH_DATA           _IOWR(MPSYS_DATA_MAGIC, 4, int)
#define MPSYS_RELEASE_DATA        _IOWR(MPSYS_DATA_MAGIC, 5, int)
#define MPSYS_WAIT_RELEASE        _IOWR(MPSYS_DATA_MAGIC, 6, int)

#define PROCESS_MAX_NUMBER 10

typedef struct
{
        int channel;
        unsigned long addr;
        int size;
        int priv;
} DataInfo;

typedef struct
{
        struct list_head list;
        pid_t tgid;
        int chn;
        int size;
        unsigned char data[0];
} DataNode;

typedef struct process_info
{
        pid_t tgid;
        int channel;
        struct completion process_done;
        int is_get;
        DataNode *p_tmp_data;
} ProcessInfo;

static ProcessInfo process_infos[PROCESS_MAX_NUMBER] = {0};

static LIST_HEAD(data_node_list);
static LIST_HEAD(release_node_list);

static DEFINE_MUTEX(process_info_mutex);
static DEFINE_MUTEX(push_data_mutex);
static DEFINE_MUTEX(data_node_mutex);
static DEFINE_MUTEX(release_node_mutex);

static DECLARE_WAIT_QUEUE_HEAD(wait_release);

static int main_process_tgid = 0;

static int get_release_list_length(void)
{
        int ret = 0;
        struct list_head *pos = NULL;

        list_for_each(pos, &release_node_list)
        {
                ret++;
        }

        return ret;
}

static int get_release_list_length_safe(void)
{
        int ret = 0;

        mpsys_mutex_lock(&release_node_mutex);
        ret = get_release_list_length();
        mpsys_mutex_unlock(&release_node_mutex);

        return ret;
}

static int mpsys_find_pinfo(int tgid, int channel)
{
        int i = 0;
        for (i = 0; i < PROCESS_MAX_NUMBER; i++)
        {
                if ((process_infos[i].tgid == current->tgid) && (process_infos[i].channel == channel))
                {
                        break;
                }
        }
        if (i != PROCESS_MAX_NUMBER)
        {
                return i;
        }
        for (i = 0; i < PROCESS_MAX_NUMBER; i++)
        {
                if ((process_infos[i].tgid == current->tgid) && (process_infos[i].channel == -1))
                {
                        process_infos[i].channel = channel;
                        break;
                }
        }
        if (i != PROCESS_MAX_NUMBER)
        {
                return i;
        }
        for (i = 0; i < PROCESS_MAX_NUMBER; i++)
        {
                if (process_infos[i].tgid == 0)
                {
                        process_infos[i].channel = channel;
                        process_infos[i].tgid = current->tgid;
                        process_infos[i].channel = channel;
                        process_infos[i].is_get = 0;
                        mpsys_init_completion(&process_infos[i].process_done);
                        break;
                }
        }
        if (i != PROCESS_MAX_NUMBER)
        {
                return i;
        }
        else
        {
                printk("cann't find ProcessInfo from process_infos\n");
                return -1;
        }
}

int get_release_state(void)
{
        return (get_release_list_length_safe() != 0);
}

static long mpsys_data_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        int ret = 0;

        switch (cmd)
        {
        case MPSYS_SET_PRODUCER:
        {
                main_process_tgid = current->tgid;
                break;
        }

        case MPSYS_SET_CONSUMER:
        {
                int i = 0;
                ProcessInfo *pProcessInfo = NULL;

                mpsys_mutex_lock(&process_info_mutex);
                for (i = 0; i < PROCESS_MAX_NUMBER; i++)
                {
                        if (process_infos[i].tgid == 0)
                        {
                                pProcessInfo = &process_infos[i];

                                pProcessInfo->is_get = 0;
                                pProcessInfo->tgid = current->tgid;
                                pProcessInfo->channel = -1;
                                mpsys_init_completion(&pProcessInfo->process_done);

                                break;
                        }
                }

                if (!pProcessInfo)
                {
                        printk("set process info failed\n");
                        ret = -EINVAL;
                        break;
                }
                mpsys_mutex_unlock(&process_info_mutex);

                break;
        }

        case MPSYS_PUSH_DATA:
        {
                int i = 0;
                int process_cnt = 0;
                int chn = 0;
                DataInfo data_info = {0};
                ProcessInfo *pProcessInfo = NULL;

                if (mpsys_mutex_lock_interruptible(&push_data_mutex))
                {
                        return -ERESTARTSYS;
                }
                mpsys_copy_from_user(&data_info, (DataInfo *)arg, sizeof(DataInfo));
                chn = data_info.channel;
                for (i = 0; i < PROCESS_MAX_NUMBER; i++)
                {
                        pProcessInfo = &process_infos[i];
                        // printk("pProcessInfo->tgid = %d, pProcessInfo->channel = %d, pProcessInfo->is_get = %d\n",
                        //              pProcessInfo->tgid, pProcessInfo->channel, pProcessInfo->is_get);

                        if ((pProcessInfo->tgid) && (pProcessInfo->channel == chn) && (pProcessInfo->is_get == 1))
                        {
                                DataNode *pDataNode = NULL;

                                pDataNode = mpsys_kzalloc(sizeof(DataNode) + data_info.size, GFP_KERNEL);
                                if (!pDataNode)
                                {
                                        return -ENOMEM;
                                }

                                pDataNode->size = data_info.size;
                                pDataNode->tgid = pProcessInfo->tgid;
                                pDataNode->chn = chn;
                                mpsys_copy_from_user(pDataNode->data, (void *)data_info.addr, data_info.size);

                                if (mpsys_mutex_lock_interruptible(&data_node_mutex))
                                {
                                        return -ERESTARTSYS;
                                }
                                list_add_tail(&pDataNode->list, &data_node_list);
                                mpsys_mutex_unlock(&data_node_mutex);

                                pProcessInfo->p_tmp_data = pDataNode;
                                pProcessInfo->is_get = 0;

                                mpsys_complete(&pProcessInfo->process_done);

                                process_cnt++;
                        }
                }
                mpsys_mutex_unlock(&push_data_mutex);

                ret = process_cnt;
                break;
        }

        case MPSYS_GET_DATA:
        {
                DataInfo data_info = {0};
                ProcessInfo *pProcessInfo = NULL;
                int chn = 0;
                int index = 0;

                mpsys_copy_from_user(&data_info, (DataInfo *)arg, sizeof(DataInfo));
                chn = data_info.channel;
                mpsys_mutex_lock(&process_info_mutex);
                index = mpsys_find_pinfo(current->tgid, chn);
                if (index >= 0)
                {
                        pProcessInfo = &process_infos[index];
                        pProcessInfo->is_get = 1;
                }
                else
                {
                        mpsys_mutex_unlock(&process_info_mutex);
                        ret = -EINVAL;
                        printk("MPSYS_GET_DATA: cann't find process_infos node\n");
                        break;
                }
                mpsys_mutex_unlock(&process_info_mutex);

                if (!pProcessInfo)
                {
                        printk("MPSYS_GET_DATA cann't find process: pid = %d\n", current->tgid);
                        ret = -1;
                        break;
                }

                ret = mpsys_wait_for_completion_interruptible(&pProcessInfo->process_done);
                if (ret)
                {
                        printk("mpsys_wait_for_completion_interruptible ret = %d\n", ret);
                        ret = -ERESTARTSYS;
                        break;
                }

                data_info.priv = main_process_tgid;
                mpsys_copy_to_user((void *)arg, &data_info, sizeof(data_info));
                mpsys_mutex_lock(&process_info_mutex);
                if (pProcessInfo->p_tmp_data)
                {
#if 0
                                printk("tgid = %d, channel = %d, get data %d, %d, %d, %d\n",
                                                        pProcessInfo->tgid,
                                                        pProcessInfo->channel,
                                                        ((int *)(pProcessInfo->p_tmp_data->data))[0],
                                                        ((int *)(pProcessInfo->p_tmp_data->data))[1],
                                                        ((int *)(pProcessInfo->p_tmp_data->data))[2],
                                                        ((int *)(pProcessInfo->p_tmp_data->data))[3]);
#endif
                        mpsys_copy_to_user((void *)(data_info.addr), pProcessInfo->p_tmp_data->data, data_info.size);
                }
                else
                {
                        ret = -EINVAL;
                }
                mpsys_mutex_unlock(&process_info_mutex);
                break;
        }

        case MPSYS_WAIT_RELEASE:
        {
                DataInfo data_info = {0};
                DataNode *pDataNode = NULL;
                int ret = 0;

                mpsys_copy_from_user(&data_info, (DataInfo *)arg, sizeof(DataInfo));

                ret = mpsys_wait_event_interruptible(&wait_release, get_release_state);
                if (-ERESTARTSYS == ret)
                {
                        printk("mpsys_wait_event_interruptible is interruptible\n");
                        return -1;
                }

                mpsys_mutex_lock(&release_node_mutex);
                pDataNode = list_first_entry(&release_node_list, DataNode, list);

                mpsys_copy_to_user((void *)(data_info.addr), pDataNode->data, data_info.size);

                list_del(&pDataNode->list);

                mpsys_kfree(pDataNode);

                mpsys_mutex_unlock(&release_node_mutex);

                break;
        }

        case MPSYS_RELEASE_DATA:
        {
                DataInfo data_info = {0};
                DataNode *pDataNode = NULL;
                unsigned char *data_tmp = NULL;
                int found = 0;
                int length = 0;
                int chn = 0;

                mpsys_copy_from_user(&data_info, (DataInfo *)arg, sizeof(DataInfo));
                if (data_info.priv != main_process_tgid)
                {
                        break;
                }
                chn = data_info.channel;
                data_tmp = mpsys_kzalloc(data_info.size, GFP_KERNEL);
                mpsys_copy_to_user(data_tmp, (void *)data_info.addr, data_info.size);

                mpsys_mutex_lock(&data_node_mutex);
                list_for_each_entry(pDataNode, &data_node_list, list)
                {
                        if ((pDataNode->tgid == current->tgid) && (pDataNode->chn == chn))
                        {
                                if (pDataNode->size == data_info.size)
                                {
                                        int i = 0;
                                        for (i = 0; i < data_info.size; i++)
                                        {
                                                if (data_tmp[i] != pDataNode->data[i])
                                                {
                                                        continue;
                                                }
                                        }

                                        if (i == data_info.size)
                                        {
                                                found = 1;
                                                break;
                                        }
                                }
                        }
                }

                mpsys_kfree(data_tmp);

                if (found == 1)
                {
                        list_del(&pDataNode->list);
                        mpsys_mutex_unlock(&data_node_mutex);

                        mpsys_mutex_lock(&release_node_mutex);
                        length = get_release_list_length();
                        list_add(&pDataNode->list, &release_node_list);
                        if (length == 0)
                        {
                                mpsys_wake_up(&wait_release);
                        }
                        mpsys_mutex_unlock(&release_node_mutex);
                }
                else
                {
                        mpsys_mutex_unlock(&data_node_mutex);
                        printk("cann't found release node!\n");
                }

                break;
        }

        default:
        {
                ret = -EINVAL;
                break;
        }
        }

        return ret;
}

static int mpsys_data_release(struct inode *inode, struct file *filp)
{
        int ret = 0;
        int length = 0;
        int i = 0;
        DataNode *pDataNode = NULL;
        DataNode *ptmp = NULL;
        int found = 0;

        mpsys_mutex_lock(&process_info_mutex);

        for (i = 0; i < PROCESS_MAX_NUMBER; i++)
        {
                if (process_infos[i].tgid == current->tgid)
                {
                        printk("find consumer process\n");
                        memset(&process_infos[i], 0, sizeof(ProcessInfo));
                        mpsys_mutex_lock(&data_node_mutex);
                        list_for_each_entry_safe(pDataNode, ptmp, &data_node_list, list)
                        {
                                if (pDataNode->tgid == current->tgid)
                                {
                                        list_del(&pDataNode->list);
                                        mpsys_mutex_lock(&release_node_mutex);
                                        length = get_release_list_length();
                                        list_add(&pDataNode->list, &release_node_list);
                                        if (length == 0)
                                        {
                                                mpsys_wake_up(&wait_release);
                                        }
                                        mpsys_mutex_unlock(&release_node_mutex);
                                }
                        }
                        mpsys_mutex_unlock(&data_node_mutex);
                        found++;
                }
        }
        mpsys_mutex_unlock(&process_info_mutex);

        if (0 == found)
        {
                if (main_process_tgid == (current->tgid))
                {
                        main_process_tgid = 0;
                        mpsys_mutex_lock(&data_node_mutex);
                        list_for_each_entry_safe(pDataNode, ptmp, &data_node_list, list)
                        {
                                list_del(&pDataNode->list);
                                mpsys_kfree(pDataNode);
                        }
                        mpsys_mutex_unlock(&data_node_mutex);
                        mpsys_mutex_lock(&release_node_mutex);
                        list_for_each_entry_safe(pDataNode, ptmp, &release_node_list, list)
                        {
                                list_del(&pDataNode->list);
                                mpsys_kfree(pDataNode);
                        }
                        mpsys_mutex_unlock(&release_node_mutex);
                }
                else
                {
                        printk("error: Unknown process\n");
                        ret = -1;
                }
        }
        return ret;
}

static struct file_operations mpsys_data_fops = {
        .owner = THIS_MODULE,
        .unlocked_ioctl = mpsys_data_ioctl,
        .release = mpsys_data_release,
};

static struct miscdevice mpsys_data_miscdev = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "mpsys_data",
        .fops = &mpsys_data_fops,
};

int mpsys_data_init(void)
{
        int ret = 0;

        ret = mpsys_misc_register(&mpsys_data_miscdev);
        if (ret)
        {
                printk("Can't register misc device mpsys_data\n");
        }

        return ret;
}

void mpsys_data_exit(void)
{
        mpsys_misc_deregister(&mpsys_data_miscdev);
}
