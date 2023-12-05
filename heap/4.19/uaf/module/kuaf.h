
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>

#define PROC_NAME "kuafproc"
#define DEVICE_NAME "kuafdevice"
#define CLASS_NAME "kuafmodule"

static struct proc_dir_entry* kuaf_proc = NULL;

struct kuaf_note
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
static struct note_data notebook[0x20];

static int __init kernel_module_init(void);
static void __exit kernel_module_exit(void);

static long kuaf_ioctl(struct file*, unsigned int, unsigned long);
static long __internal_kuaf_ioctl(struct file* __file, unsigned int cmd, long data);

static int kuaf_open(struct inode*, struct file*);
static ssize_t kuaf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff);
static int kuaf_release(struct inode *, struct file *);

static int kuaf_create(size_t, size_t, char*);
static int kuaf_edit(size_t, char*);
static int kuaf_free(size_t);
static int kuaf_gift(char*);

static struct file_operations kuaf_module_fo = 
{
    .unlocked_ioctl = kuaf_ioctl,
    .open = kuaf_open,
    .read = kuaf_read,
    .release = kuaf_release,
};

