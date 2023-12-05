
//db_fetch.ko

#include "db_fetch.h"

module_init(kernel_module_init);
module_exit(kernel_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");

static int db_fetch_flag_read(void)
{
    // 保存打开文件的文件指针变量
    struct file* file = NULL;
    loff_t pos = 0;
    int ret = 0;
    
    // 打开文件
    file = filp_open("/flag", O_RDONLY, 0);
    if(IS_ERR(file))
    {
      printk(KERN_INFO "[db_fetch] Flag open fail!\n");
      return -1;
    }
    
    // 读操作
    ret = kernel_read(file, db_fetch_flag, 0x20, &pos);
    printk(KERN_INFO "[db_fetch] Flag: %s\n", db_fetch_flag);
    printk(KERN_INFO "[db_fetch] Kernel_read return ret: %d\n", ret);
    
    // 关闭文件
    filp_close(file, NULL);
    return 0;
}

// 初始化
static int __init kernel_module_init(void)
{
    if (db_fetch_flag == NULL)
    {    
        db_fetch_flag = kmalloc(0x20, GFP_ATOMIC);
        if (db_fetch_flag == NULL)
        {
            printk(KERN_INFO "[db_fetch] Unable to initialize the buffer. Kernel malloc error.\n");
            return -1;
        }
        memset(db_fetch_flag, 0, 0x20);
        printk(KERN_INFO "[db_fetch] Buffer initialized successfully.\n");
        db_fetch_flag_read();
    }

    db_fetch_proc = proc_create(PROC_NAME, 0666, NULL, &db_fetch_module_fo);
    return 0;
}

// 注销
static void __exit kernel_module_exit(void)
{
    printk(KERN_INFO "[db_fetch] Start to clean up the module.\n");
    remove_proc_entry(PROC_NAME, NULL);
    if (db_fetch_flag)
    {
        kfree(db_fetch_flag);
        db_fetch_flag = NULL;
    }
    printk(KERN_INFO "[db_fetch] Module clean up complete. See you next time.\n");
}

// 接口
static long db_fetch_ioctl(struct file* __file, unsigned int cmd, unsigned long data)
{
    long ret;
    ret = __internal_db_fetch_ioctl(__file, cmd, data);
    return ret;
}
static long __internal_db_fetch_ioctl(struct file* __file, unsigned int cmd, long data)
{
    printk(KERN_INFO "[db_fetch] Received operation code: %d\n", cmd);
    switch(cmd)
    {
        case 0x112233:
            db_fetch_get_flag_addr((char*)data);
            break;

        case 0x223344:
            db_fetch_get_flag((struct flag_info*)data);
            break;

        default:
            printk(KERN_INFO "[db_fetch] Invalid operation code.\n");
            return -1;
    }
    return 0;
}

static noinline bool chk_range_ok(char* addr)
{
    if(addr > (char*)0x7ffffffff000)
    {
        printk(KERN_INFO "[db_fetch] Too long.\n");
        return 0;
    }
    else
        return 1;
}

static noinline int db_fetch_get_flag_addr(char* buf)
{
    char kbuf[0x30];
    int count;
    snprintf(kbuf, 0x30, "Your flag is at %px.\n", db_fetch_flag);

    count = copy_to_user(buf, kbuf, 0x30);
    return count;
}

static noinline int db_fetch_get_flag(struct flag_info* addr) 
{
    char kbuf[0x30];
    int count,i;
    if(!chk_range_ok(addr->flag_addr))
    {
        return -1;
    }
    printk(KERN_INFO "[db_fetch] Start comparing flags.\n");
    for (i = 0; i < strlen(db_fetch_flag); ++i)
    {
        // 如果来到这一步,有竞争线程把 addr->flag_addr 改成了 flag 地址就会通过判断
        if((addr->flag_addr)[i] != db_fetch_flag[i]) 
            return -1;
    }
    printk(KERN_INFO "[db_fetch] Get flag!\n");
    snprintf(kbuf, 0x30, "So here is flag: %s\n", db_fetch_flag);
    printk(KERN_INFO "[db_fetch] %s\n", kbuf);
    count = copy_to_user(addr->flag_data, kbuf, 0x30);
    return count;
}



static int db_fetch_open(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[db_fetch] Device open.\n");
    return 0;
}


static int db_fetch_release(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[db_fetch] Device closed.\n");
    return 0;
}