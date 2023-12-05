
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>

#define PROC_NAME "hovfproc"
#define DEVICE_NAME "hovfdevice"
#define CLASS_NAME "hovfmodule"

static struct proc_dir_entry* hovf_proc = NULL;

struct user_req_t
{
    size_t idx;
    size_t size;
    char* buf;
};

struct note_data
{
    char* data;
};

// typedef struct
// {
//     char pad[6];
//     char data[];
// }hovf_note_t;

struct hovf_cache
{
    char buf[0x200];
};

static struct note_data notebook[400];
static struct kmem_cache* hovf_cachep = NULL;
static DEFINE_MUTEX(hovf_lock);

static int __init kernel_module_init(void);
static void __exit kernel_module_exit(void);

static long hovf_ioctl(struct file*, unsigned int, unsigned long);
static long __internal_hovf_ioctl(struct file* __file, unsigned int cmd, long data);

static int hovf_open(struct inode*, struct file*);
static ssize_t hovf_read(struct file* __file, char __user* user_buf, size_t idx, loff_t* __loff);
static int hovf_release(struct inode *, struct file *);

static int hovf_create(size_t);
static int hovf_edit(size_t, char*, size_t);

static struct file_operations hovf_module_fo = 
{
    .unlocked_ioctl = hovf_ioctl,
    .open = hovf_open,
    .read = hovf_read,
    .release = hovf_release,
};

