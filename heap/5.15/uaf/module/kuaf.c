
//kuaf.ko

#include "kuaf.h"

module_init(kernel_module_init);
module_exit(kernel_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");

//初始化
static int __init kernel_module_init(void)
{
    kuaf_proc = proc_create(PROC_NAME, 0666, NULL, &kuaf_module_fo);
    return 0;
}

//注销
static void __exit kernel_module_exit(void)
{
    printk(KERN_INFO "[kuaf] Start to clean up the module.\n");
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "[kuaf] Module clean up complete. See you next time.\n");
}

//接口
static long kuaf_ioctl(struct file* __file, unsigned int cmd, unsigned long data)
{
    long ret;
    ret = __internal_kuaf_ioctl(__file, cmd, data);
    return ret;
}

static long __internal_kuaf_ioctl(struct file* __file, unsigned int cmd, long data)
{
    int count;
    struct kuaf_note kernel_note;
    count = copy_from_user(&kernel_note, (struct kuaf_note*)data, sizeof(struct kuaf_note));
    printk(KERN_INFO "[kuaf] Received operation code: %d\n", cmd);
    switch(cmd)
    {
        case 0x112233:
            kuaf_create(kernel_note.idx, kernel_note.size, kernel_note.buf);
            break;

        case 0x223344:
            kuaf_edit(kernel_note.idx, kernel_note.buf);
            break;

        case 0x334455:
            kuaf_free(kernel_note.idx);
            break;

        case 0x445566:
            kuaf_gift(kernel_note.buf);
            break;

        default:
            printk(KERN_INFO "[kuaf] Invalid operation code.\n");
            return -1;
    }
    return count;
}

static noinline int kuaf_create(size_t idx, size_t size, char* buf)
{
    int count;
    struct note_data* obj;
    if(idx > 0x1f)
    {
        printk(KERN_INFO "[kuaf] Add idx out of range.\n");
        return -1;
    }
    else
    {
        if(size > 0x1000) //0x1000, 0x300
        {
            printk(KERN_INFO "[kuaf] Add size out of range.\n");  
            return -1; 
        }
        else
        {
            obj = &notebook[idx];
            if(obj->size)
            {
                printk(KERN_INFO "[kuaf] Add idx is not empty.\n");
                return -1;
            }
            obj->size = size;
            // obj->data = kmalloc(size, GFP_ATOMIC);
            obj->data = kmalloc(size, GFP_KERNEL_ACCOUNT);
            count = copy_from_user(obj->data, buf, size);
            return count;
        }
    }
    
}

static noinline int kuaf_edit(size_t idx, char* buf)
{
    int count;
    struct note_data* obj;

    if(idx > 0x1f)
    {
        printk(KERN_INFO "[kuaf] Edit idx out of range.\n");
        return -1;
    }

    obj = &notebook[idx];
    count = copy_from_user(obj->data, buf, obj->size);
    return count;

}

static noinline int kuaf_free(size_t idx)
{
    struct note_data* obj;

    if(idx > 0x1f)
    {
        printk(KERN_INFO "[kuaf] Delete idx out of range.\n");
        return -1;
    }

    obj = &notebook[idx];
    kfree(obj->data);
    printk(KERN_INFO "[kuaf] Delete success.\n");
    return 0;

}

//获取到 notebook 中所有信息
static noinline int kuaf_gift(char* buf)
{
    int count;
    printk("[kuaf] The notebook needs to be written from beginning to end.\n");
    count = copy_to_user(buf, notebook, 0x100);
    printk("[kuaf] Notebook addr: %px\n", &notebook);
    return count;
}

static int kuaf_open(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[kuaf] Device open.\n");
    return 0;
}

static int kuaf_release(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[kuaf] Device closed.\n");
    return 0;
}

//notebook[idx].data 数据传递到用户空间
static ssize_t kuaf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff)
{
    int count;
    size_t size;
    char* data;
    if(idx > 0x1f)
    {
        printk(KERN_INFO "[kuaf] Read idx out of range.\n");
        return -1;
    }
    else
    {
        size = notebook[idx].size;
        data = notebook[idx].data;
        count = copy_to_user(user_buf, data, size);
        printk(KERN_INFO "[kuaf] Read success.\n");
        return count;
    }
}


