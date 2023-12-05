
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>

#define PROC_NAME "userfproc"
#define DEVICE_NAME "userfdevice"
#define CLASS_NAME "userfmodule"

static struct proc_dir_entry* userf_proc = NULL;
static char userfbuf[0x100];
static rwlock_t lock;

struct userf_note
{
    size_t idx;
    size_t size;
    char* buf;
};

struct note_data
{
    size_t size;
    char* data;
};
static struct note_data notebook[0x10];

static int __init kernel_module_init(void);
static void __exit kernel_module_exit(void);

static long userf_ioctl(struct file*, unsigned int, unsigned long);
static long __internal_userf_ioctl(struct file* __file, unsigned int cmd, long data);

static int userf_open(struct inode*, struct file*);
static ssize_t userf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff);
static ssize_t userf_write(struct file* __file, const char __user* user_buf, size_t idx, loff_t* __loff);
static int userf_release(struct inode *, struct file *);

static int userf_create(size_t, size_t, char*);
static int userf_edit(size_t, size_t, char*);
static int userf_free(size_t);
static int userf_gift(char*);

static struct file_operations userf_module_fo = 
{
    .unlocked_ioctl = userf_ioctl,
    .open = userf_open,
    .read = userf_read,
    .write = userf_write,
    .release = userf_release,
};
