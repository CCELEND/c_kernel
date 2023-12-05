
// hovf.ko
#include "hovf.h"

module_init(kernel_module_init);
module_exit(kernel_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");

// 驱动初始化
static int __init kernel_module_init(void)
{
    mutex_init(&hovf_lock);
    hovf_proc = proc_create(PROC_NAME, 0666, NULL, &hovf_module_fo);
    // hovf_cachep = kmem_cache_create("hovf_cachep", 0x200LL, 1LL, SLAB_ACCOUNT | SLAB_PANIC, 0LL);
    hovf_cachep = KMEM_CACHE(hovf_cache, SLAB_PANIC | SLAB_ACCOUNT);
    if(!hovf_cachep){
        printk(KERN_INFO "[hovf] Allocation error.\n");
        return -1;
    }
    return 0;
}

// 驱动注销
static void __exit kernel_module_exit(void)
{
    printk(KERN_INFO "[hovf] Start to clean up the module...\n");
    mutex_destroy(&hovf_lock);
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "[hovf] Module clean up complete. See you next time.\n");
}

// 接口
static long hovf_ioctl(struct file* __file, unsigned int cmd, unsigned long data)
{
    long ret;
    ret = __internal_hovf_ioctl(__file, cmd, data);
    return ret;
}

static long __internal_hovf_ioctl(struct file* __file, unsigned int cmd, long data)
{
    int count;
    struct user_req_t kernel_note;
    count = copy_from_user(&kernel_note, (struct user_req_t*)data, sizeof(struct user_req_t));
    if(count)
        return -1;
    printk(KERN_INFO "[hovf] Received operation code: %d\n", cmd);
    mutex_lock(&hovf_lock);
    switch(cmd)
    {
        case 0x112233:
            hovf_create(kernel_note.idx);
            break;

        case 0x223344:
            hovf_edit(kernel_note.idx, kernel_note.buf, kernel_note.size);
            break;

        default:
            printk(KERN_INFO "[hovf] Invalid operation code.\n");
            count = -1;
    }
    mutex_unlock(&hovf_lock);
    return count;
}

static noinline int hovf_create(size_t idx)
{
    struct note_data* obj;
    if(idx >= 400)
    {
        printk(KERN_INFO "[hovf] Add idx out of range.\n");
        return -1;
    }
    else
    {
        obj = &notebook[idx];
        if(obj->data)
        {
            printk(KERN_INFO "[hovf] Add idx is not empty.\n");
            return -1;
        }
        // obj->data = kmalloc(size, GFP_ATOMIC); // GFP_ATOMIC GFP_KERNEL_ACCOUNT
        obj->data = kmem_cache_zalloc(hovf_cachep, GFP_KERNEL_ACCOUNT);
        return 0;
    }
}

static noinline int hovf_edit(size_t idx, char* buf, size_t size)
{
    struct note_data* obj;
    char src[0x200];

    obj = &notebook[idx];

    if(idx >= 400 || !obj->data)
    {
        goto edit_fail;
    }
    if(size > 0x200 || copy_from_user(src, buf, size))
    {
        goto edit_fail;
    }
    // 存在内核堆溢出
    memcpy((char*)(obj->data + 6), src, size);

    return size;

    edit_fail:
    printk(KERN_INFO "[hovf] Chunk editing failed!\n");
    return -1;
}

static int hovf_open(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[hovf] Device open.\n");
    return 0;
}

static int hovf_release(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[hovf] Device closed.\n");
    return 0;
}

// notebook[idx].data 数据传递到用户空间
static ssize_t hovf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff)
{
    int count;
    char* data;
    if(idx > 0x18f)
    {
        printk(KERN_INFO "[hovf] Read idx out of range.\n");
        return -1;
    }
    else
    {
        data = notebook[idx].data;
        count = copy_to_user(user_buf, data, 0x200);
        printk(KERN_INFO "[hovf] Read success.\n");
        return count;
    }
}


