#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

#define DEVICE_NAME "hovf"
#define CLASS_NAME  "hovf"

#define OVERFLOW_SZ 0x6

#define CHUNK_SIZE 512
#define MAX 8 * 50

#define ALLOC 0x112233
#define EDIT 0x223344

MODULE_DESCRIPTION("a hovf cache, a secluded slab, a marooned memory");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");

typedef struct
{
    int64_t idx;
    uint64_t size;
    char* buf;    
}user_req_t;

typedef struct
{
    char pad[OVERFLOW_SZ];
    char buf[];
}hovf_t;

struct hovf_cache
{
    char buf[CHUNK_SIZE];
};

static DEFINE_MUTEX(hovf_lock);

int hovf_ctr = 0;

hovf_t **hovf_arr;

static long hovf_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static long hovf_add(void);
static long hovf_edit(int64_t idx, uint64_t size, char *buf);


static struct miscdevice hovf_dev;
static struct file_operations hovf_fops = { .unlocked_ioctl = hovf_ioctl };
static struct kmem_cache *hovf_cachep;

static long hovf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    user_req_t req;
    long ret = 0;

    if (cmd != ALLOC && copy_from_user(&req, (void *)arg, sizeof(req)))
    {
        return -1;
    }
    printk(KERN_INFO "[hovf] Received operation code: %d\n", cmd);
    mutex_lock(&hovf_lock);
    switch (cmd)
    {
        case ALLOC:
            ret = hovf_add();
            break;
        case EDIT:
            ret = hovf_edit(req.idx, req.size, req.buf);
            break;
        default:
            printk(KERN_INFO "[hovf] Invalid operation code.\n");
            ret = -1;
    }
    mutex_unlock(&hovf_lock);
    return ret;
}

// 返回分配 chunk 的 idx
static long hovf_add(void)
{
    int idx;
    if (hovf_ctr >= MAX)
    {
        goto failure_add;
    }
    idx = hovf_ctr++;
    hovf_arr[idx] = kmem_cache_zalloc(hovf_cachep, GFP_KERNEL_ACCOUNT);

    if (!hovf_arr[idx])
    {
        goto failure_add;
    }

    return idx;

    failure_add:
    printk(KERN_INFO "[hovf] Chunk allocation failed!\n");
    return -1;
}

// 返回成功修改的数据大小
static long hovf_edit(int64_t idx, uint64_t size, char *buf)
{
    char temp[CHUNK_SIZE];
    if (idx < 0 || idx >= MAX || !hovf_arr[idx])
    {
        goto edit_fail;
    }
    if (size > CHUNK_SIZE || copy_from_user(temp, buf, size))
    {
        goto edit_fail;
    }
    memcpy(hovf_arr[idx]->buf, temp, size);

    return size;

    edit_fail:
    printk(KERN_INFO "[hovf] Chunk editing failed!\n");
    return -1;
}

static int init_hovf_driver(void)
{
    hovf_dev.minor = MISC_DYNAMIC_MINOR;
    hovf_dev.name = DEVICE_NAME;
    hovf_dev.fops = &hovf_fops;
    hovf_dev.mode = 0644;
    printk(KERN_INFO "[hovf] Module initialization... \n");
    mutex_init(&hovf_lock);
    if (misc_register(&hovf_dev))
    {
        return -1;
    }
    hovf_arr = kzalloc(MAX * sizeof(hovf_t *), GFP_KERNEL);
    if (!hovf_arr)
    {
        return -1;
    }
    hovf_cachep = KMEM_CACHE(hovf_cache, SLAB_PANIC | SLAB_ACCOUNT);
    if (!hovf_cachep)
    {
        return -1;
    }
    printk(KERN_INFO "[hovf] Module initialization completed.\n");
    return 0;
}

static void cleanup_hovf_driver(void)
{
    int i;
    printk(KERN_INFO "[hovf] Start to clean up the module...\n");
    misc_deregister(&hovf_dev);
    mutex_destroy(&hovf_lock);
    for (i = 0; i < MAX; i++)
    {
        if (hovf_arr[i])
        {
            kfree(hovf_arr[i]);
        }
    }
    kfree(hovf_arr);
    printk(KERN_INFO "[hovf] Module clean up complete.\n");
    printk(KERN_INFO "[hovf] Guess you remain a hovf.\n");
}

module_init(init_hovf_driver);
module_exit(cleanup_hovf_driver);