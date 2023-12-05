
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>

#define PROC_NAME "ovfproc"
#define DEVICE_NAME "ovfdevice"
#define CLASS_NAME "ovfmodule"

static struct proc_dir_entry* ovf_proc = NULL;
static void* ovfbuf = NULL;
static unsigned int offset;

static int __init kernel_module_init(void);
static void __exit kernel_module_exit(void);

static long ovf_ioctl(struct file*, unsigned int, unsigned long);
static long __internal_ovf_ioctl(struct file* __file, unsigned int cmd, long data);

static int ovf_open(struct inode*, struct file*);
static ssize_t ovf_read(struct file* __file, char __user* user_buf, size_t size, loff_t* __loff);
static ssize_t ovf_write(struct file* __file, const char __user* user_buf, size_t size, loff_t* __loff);
static int ovf_release(struct inode *, struct file *);

static long ovf_read_func(char* buf);
static char ovf_copy_func(long data);

static struct proc_ops ovf_module_fo = 
{
    .proc_ioctl = ovf_ioctl,
    .proc_open = ovf_open,
    .proc_read = ovf_read,
    .proc_write = ovf_write,
    .proc_release = ovf_release,
};
