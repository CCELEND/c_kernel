
//ovf.ko

#include "ovf.h"

module_init(kernel_module_init);
module_exit(kernel_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");


//初始化
static int __init kernel_module_init(void)
{
    ovf_proc = proc_create(PROC_NAME, 0666, NULL, &ovf_module_fo);
    return 0;
}

//注销
static void __exit kernel_module_exit(void)
{
    printk(KERN_INFO "[easy over] Start to clean up the module.\n");
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "[easy over] Module clean up complete. See you next time.\n");
}

//接口
static long ovf_ioctl(struct file* __file, unsigned int cmd, unsigned long data)
{
    long ret;
    ret = __internal_ovf_ioctl(__file, cmd, data);
    return ret;
}

static long __internal_ovf_ioctl(struct file* __file, unsigned int cmd, long data)
{
    char chr0;
    printk(KERN_INFO "[easy over] Received operation code: %d\n", cmd);
    switch(cmd)
    {
        case 0x112233:
            ovf_read_func((char*)data);
            break;

        case 0x223344:
            chr0 = ovf_copy_func(data);
            printk(KERN_INFO "[easy over] chr0: %c\n", chr0);
            break;

        case 0x334455:
            printk(KERN_INFO "[easy over] Set offset.\n");
            offset = data;
            break;

        default:
            printk(KERN_INFO "[easy over] Invalid operation code.\n");
            return -1;
    }
    return 0;
}

static noinline long ovf_read_func(char* buf)
{
    char kbuf[0x40];
    long count;

    count = copy_to_user(buf, &kbuf[offset], 0x40);
    return count;
}

static noinline char ovf_copy_func(long len)
{
    char kbuf[0x40];
    memcpy(kbuf, "CCELEND", 0x7);
    if(len > 63)
    {
        printk(KERN_INFO "[easy over] Too long.\n");
        return -1;
    }
    else
    {
        memcpy(kbuf, ovfbuf, (unsigned short)len);
        return kbuf[0];
    }
}

//初始化内核堆
static int ovf_open(struct inode * __inode, struct file * __file)
{
    if (ovfbuf == NULL)
    {    
        ovfbuf = kmalloc(0x200, GFP_ATOMIC);
        if (ovfbuf == NULL)
        {
            printk(KERN_INFO "[easy over] Unable to initialize the buffer. Kernel malloc error.\n");
            return -1;
        }
        memset(ovfbuf, 1, 0x200);
        printk(KERN_INFO "[easy over] Device open, buffer initialized successfully.\n");
    }
    else
    {
        printk(KERN_INFO "[easy over] Warning: reopen the device may cause unexpected error in kernel.\n");
    }

    return 0;
}

//内核空间释放
static int ovf_release(struct inode* __inode, struct file* __file)
{
    if (ovfbuf)
    {
        kfree(ovfbuf);
        ovfbuf = NULL;
    }
    printk(KERN_INFO "[easy over] Device closed.\n");
    return 0;
}

//ovfbuf 数据传递到用户空间
static ssize_t ovf_read(struct file* __file, char __user* user_buf, size_t size, loff_t* __loff)
{
    char* const buf = ovfbuf;
    int count;

    count = copy_to_user(user_buf, buf, size > 0x200 ? 0x200 : size);
    return count;
}

//读取用户空间数据到 ovfbuf
static ssize_t ovf_write(struct file* __file, const char __user* user_buf, size_t size, loff_t* __loff)
{
    char* const buf = ovfbuf;
    int count;

    count = copy_from_user(buf, user_buf, size > 0x200 ? 0x200 : size);
    return count;
}
