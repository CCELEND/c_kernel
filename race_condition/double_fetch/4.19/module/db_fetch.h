
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/slab.h>

#define PROC_NAME "db_fetchproc"
#define DEVICE_NAME "db_fetchdevice"
#define CLASS_NAME "db_fetchmodule"

struct flag_info
{
    char* flag_addr;
    char* flag_data;
};

static struct proc_dir_entry* db_fetch_proc = NULL;
static char* db_fetch_flag = NULL;

static int __init kernel_module_init(void);
static void __exit kernel_module_exit(void);

static long db_fetch_ioctl(struct file*, unsigned int, unsigned long);
static long __internal_db_fetch_ioctl(struct file* __file, unsigned int cmd, long data);

static int db_fetch_open(struct inode*, struct file*);
static int db_fetch_release(struct inode*, struct file*);


static int db_fetch_get_flag_addr(char* buf);
static int db_fetch_get_flag(struct flag_info*);

static struct file_operations db_fetch_module_fo = 
{
    .unlocked_ioctl = db_fetch_ioctl,
    .open = db_fetch_open,
    .release = db_fetch_release,
};


