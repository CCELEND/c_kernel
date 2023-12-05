
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>

#define PROC_NAME "suserfproc"
#define DEVICE_NAME "suserfdevice"
#define CLASS_NAME "suserfmodule"

static struct proc_dir_entry* suserf_proc = NULL;

struct suserf_note
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

static long suserf_ioctl(struct file*, unsigned int, unsigned long);
static long __internal_suserf_ioctl(struct file* __file, unsigned int cmd, long data);

static int suserf_open(struct inode*, struct file*);
static ssize_t suserf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff);
static int suserf_release(struct inode *, struct file *);

static int suserf_create(size_t, size_t, char*);
// static int suserf_edit(size_t, char*, size_t);
static int suserf_free(size_t, char*);

static struct file_operations suserf_module_fo = 
{
    .unlocked_ioctl = suserf_ioctl,
    .open = suserf_open,
    .read = suserf_read,
    .release = suserf_release,
};
