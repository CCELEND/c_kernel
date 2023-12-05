
// userf.ko

#include "userf.h"

module_init(kernel_module_init);
module_exit(kernel_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccelend");

// 初始化
static int __init kernel_module_init(void)
{
    rwlock_init(&lock);
    userf_proc = proc_create(PROC_NAME, 0666, NULL, &userf_module_fo);
    return 0;
}

// 注销
static void __exit kernel_module_exit(void)
{
    printk(KERN_INFO "[userf] Start to clean up the module.\n");
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "[userf] Module clean up complete. See you next time.\n");
}

// 接口
static long userf_ioctl(struct file* __file, unsigned int cmd, unsigned long data)
{
    long ret;
    ret = __internal_userf_ioctl(__file, cmd, data);
    return ret;
}

static long __internal_userf_ioctl(struct file* __file, unsigned int cmd, long data)
{

    struct userf_note kernel_note;
    copy_from_user(&kernel_note, (struct userf_note*)data, sizeof(struct userf_note));
    printk(KERN_INFO "[userf] Received operation code: %d\n", cmd);
    switch(cmd)
    {
        case 0x112233:
            userf_create(kernel_note.idx, kernel_note.size, kernel_note.buf);
            break;

        case 0x223344:
            userf_edit(kernel_note.idx, kernel_note.size, kernel_note.buf);
            break;

        case 0x334455:
            userf_free(kernel_note.idx);
            break;

        case 0x445566:
            userf_gift(kernel_note.buf);
            break;

        default:
            printk(KERN_INFO "[userf] Invalid operation code.\n");
            return -1;
    }
    return 0;
}

static noinline int userf_create(size_t idx, size_t size, char* buf)
{
    int count;
    size_t size_temp;
    struct note_data* obj;
    if(idx > 0xf)
    {
        printk(KERN_INFO "[userf] Add idx out of range.\n");
        return -1;
    }
    else
    {
        obj = &notebook[idx];
        _raw_read_lock(&lock);
        // 会先修改 notebook 的 size 
        size_temp = obj->size;
        obj->size = size;

        if(size > 0x60)
        {
            obj->size = size_temp;
            count = -2;
            printk(KERN_INFO "[userf] Add size out of range.\n");   
        }
        else
        {
            copy_from_user(userfbuf, buf, 0x100);
            if(obj->data)
            {
                obj->size = size_temp;
                count = -3;
                printk(KERN_INFO "[userf] Add idx is not empty.\n");
            }
            else
            {
                obj->data = kmalloc(size, GFP_ATOMIC);
                printk(KERN_INFO "[userf] Add success. %s left a note.\n", userfbuf);
                count = 0;
            }

        }
        _raw_read_unlock(&lock);
    }
    return count;
}

// userf_edit 函数，其中并没有对新分配的内存大小进行限制
static noinline int userf_edit(size_t idx, size_t new_size, char* buf)
{
    struct note_data* obj;
    char* new_data;
    size_t size;
    int count;
    if(idx > 0xf)
    {
        printk(KERN_INFO "[userf] Edit idx out of range.\n");
        return -1;
    }

    obj = &notebook[idx];
    _raw_read_lock(&lock);
    size = obj->size;
    obj->size = new_size;
    if(size == new_size)
    {
        count = 1;
        goto editout;
    }

    // 当传入的 new_size 为0时，会被当做 free 处理释放空间
    new_data = krealloc(obj->data, new_size, GFP_ATOMIC);
    copy_from_user(userfbuf, buf, 0x100);
    if(!obj->size)
    {
        printk(KERN_INFO "[userf] Free in fact.\n");
        obj->data = NULL;
        count = 0;
        goto editout;
    }

    if(__virt_addr_valid((long unsigned int)new_data))
    {
        obj->data = new_data;
        count = 2;
    editout:
        _raw_read_unlock(&lock);
        printk(KERN_INFO "[userf] Edit success. %s edit a note.\n", userfbuf);
        return count;
    }

    printk(KERN_INFO "[userf] Return ptr unvalid.\n");
    _raw_read_unlock(&lock);
    return 3;

}

static noinline int userf_free(size_t idx)
{
    struct note_data* obj;

    if(idx > 0xf)
    {
        printk(KERN_INFO "[userf] Delete idx out of range.\n");
        return -1;
    }

    _raw_write_lock(&lock);
    obj = &notebook[idx];
    kfree(obj->data);
    if(obj->size)
    {
        obj->size = 0;
        obj->data = NULL;
    }
    _raw_write_unlock(&lock);

    printk(KERN_INFO "[userf] Delete success.\n");
    return 0;

}

// 获取到 notebook 中所有信息
static noinline int userf_gift(char* buf)
{
    printk("[userf] The notebook needs to be written from beginning to end.\n");
    copy_to_user(buf, notebook, 0x100);
    printk("[userf] For this special year, I give you a gift!\n");
    return 0;
}

static int userf_open(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[userf] Device open.\n");
    return 0;
}

static int userf_release(struct inode* __inode, struct file* __file)
{
    printk(KERN_INFO "[userf] Device closed.\n");
    return 0;
}

// notebook[idx].data 数据传递到用户空间
static ssize_t userf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff)
{
    int count;
    size_t size;
    char* data;
    if(idx > 0xf)
    {
        printk(KERN_INFO "[userf] Read idx out of range.\n");
        return -1;
    }
    else
    {
        size = notebook[idx].size;
        data = notebook[idx].data;
        count = copy_to_user(user_buf, data, size);
        printk(KERN_INFO "[userf] Read success.\n");
        return 0;
    }
}

// 读取用户空间数据到 notebook[idx].data
static ssize_t userf_write(struct file* __file, const char __user* user_buf, size_t idx, loff_t* __loff)
{
    int count;
    size_t size;
    char* data;
    if(idx > 0xf)
    {
        printk(KERN_INFO "[userf] Write idx out of range.\n");
        return -1;
    }
    else
    {
        size = notebook[idx].size;
        data = notebook[idx].data;    
        count = copy_from_user(data, user_buf, size);
        if(count)
            printk(KERN_INFO "[userf] copy from user error.\n");
        else
            printk(KERN_INFO "[userf] Write success.\n");
        return 0;
    }
}
