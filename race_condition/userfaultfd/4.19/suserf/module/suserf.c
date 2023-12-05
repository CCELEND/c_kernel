
//suserf.ko

#include "suserf.h"

module_init(kernel_module_init);
module_exit(kernel_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");

//初始化
static int __init kernel_module_init(void)
{
    suserf_proc = proc_create(PROC_NAME, 0666, NULL, &suserf_module_fo);
    return 0;
}

// 注销
static void __exit kernel_module_exit(void)
{
    printk(KERN_INFO "[suserf] Start to clean up the module...\n");
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "[suserf] Module clean up complete. See you next time.\n");
}

// 接口
static long suserf_ioctl(struct file* __file, unsigned int cmd, unsigned long data)
{
    long ret;
    ret = __internal_suserf_ioctl(__file, cmd, data);
    return ret;
}

static long __internal_suserf_ioctl(struct file* __file, unsigned int cmd, long data)
{
    int count;
    struct suserf_note kernel_note;
    count = copy_from_user(&kernel_note, (struct suserf_note*)data, sizeof(struct suserf_note));
    printk(KERN_INFO "[suserf] Received operation code: %d\n", cmd);
    switch(cmd)
    {
        case 0x112233:
            suserf_create(kernel_note.idx, kernel_note.size, kernel_note.buf);
            break;

        // case 0x223344:
        //     suserf_edit(kernel_note.idx, kernel_note.buf, kernel_note.size);
        //     break;

        case 0x334455:
            suserf_free(kernel_note.idx, kernel_note.buf);
            break;

        default:
            printk(KERN_INFO "[suserf] Invalid operation code!\n");
            return -1;
    }
    return count;
}

static noinline int suserf_create(size_t idx, size_t size, char* buf)
{
    int count;
    struct note_data* obj;
    if(idx > 0x1f)
    {
        printk(KERN_INFO "[suserf] Add idx out of range!\n");
        return -1;
    }
    else
    {
        if(size > 0x1000) //0x1000, 0x300
        {
            printk(KERN_INFO "[suserf] Add size out of range!\n");  
            return -1; 
        }
        else
        {
            obj = &notebook[idx];
            if(obj->size)
            {
                printk(KERN_INFO "[suserf] Add idx is not empty!\n");
                return -1;
            }
            obj->size = size;
            obj->data = kmalloc(size, GFP_ATOMIC); //GFP_ATOMIC
            count = copy_from_user(obj->data, buf, size);
            return count;
        }
    }
    
}

// static noinline int suserf_edit(size_t idx, char* buf, size_t size)
// {
//     int count;
//     struct note_data* obj;
//     size_t rel_size;

//     if(idx > 0x1f)
//     {
//         printk(KERN_INFO "[suserf] Edit idx out of range!\n");
//         return -1;
//     }

//     obj = &notebook[idx];
//     rel_size = notebook[idx].size;
//     rel_size = rel_size > size ? size : rel_size;
//     count = copy_from_user(obj->data, buf, rel_size);
//     return count;
// }

static noinline int suserf_free(size_t idx, char* buf)
{
    struct note_data* obj;
    int count;

    if(idx > 0x1f)
    {
        printk(KERN_INFO "[suserf] Delete idx out of range!\n");
        return -1;
    }

    obj = &notebook[idx];
    kfree(obj->data);
    count = copy_to_user(buf, obj->data, obj->size);
    obj->size = 0;
    obj->data = 0;
    printk(KERN_INFO "[suserf] Delete success.\n");
    return count;
}

static int suserf_open(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[suserf] Device open.\n");
    return 0;
}

static int suserf_release(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[suserf] Device closed.\n");
    return 0;
}

//notebook[idx].data 数据传递到用户空间
static ssize_t suserf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff)
{
     int count;
     size_t size;
     char* data;
     if(idx > 0x1f)
     {
         printk(KERN_INFO "[suserf] Read idx out of range!\n");
         return -1;
     }
     else
     {
         size = notebook[idx].size;
         data = notebook[idx].data;
         count = copy_to_user(user_buf, data, size);
         printk(KERN_INFO "[suserf] Read success.\n");
         return count;
     }
 }


