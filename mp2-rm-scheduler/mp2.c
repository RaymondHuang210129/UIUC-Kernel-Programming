#define LINUX

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "mp2_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tse-Jui Huang");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1

/* section: variable initialization */

static struct proc_dir_entry* mp2_proc;
static struct proc_dir_entry* mp2_dir;
static char * input_str;

static int mp2_show(struct seq_file * m, void * v)
{
   return 0;
}

static int mp2_open(struct inode *inode, struct file *file)
{
   return single_open(file, mp2_show, NULL);
}

ssize_t mp2_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
{
    if (input_str != NULL) {
		printk(KERN_ALERT "input_str is not null.\n");
		kfree(input_str);
		input_str = NULL;
	} else {
		printk(KERN_ALERT "input_str is null.\n");
	}

    printk(KERN_ALERT "size passed from parameter: %lu\n", size);
	input_str = kzalloc(size + 1, GFP_KERNEL);
    if (input_str == NULL)
	{
		printk(KERN_ALERT "unable to kzalloc.\n");
		return -ENOMEM;
	}

    printk(KERN_ALERT "copy from user.\n");
	if (copy_from_user(input_str, ubuf, size))
	{
		printk(KERN_ALERT "copy from user error.\n");
		return EFAULT;
	}

    /* section: identify command */
    if (strlen(input_str) >= 13 && strncmp(input_str, "Registration", 12) == 0) {
        printk(KERN_ALERT "app registration\n");
    } else if (strlen(input_str) >= 6 && strncmp(input_str, "Yield", 5) == 0) {
        printk(KERN_ALERT "app yield\n");
    } else if (strlen(input_str) >= 15 && strncmp(input_str, "Deregistration", 14) == 0) {
        printk(KERN_ALERT "app deregistration\n");
    } else {
        printk(KERN_ALERT "input unknown command\n");
    }

    return (ssize_t)size;
}

static const struct file_operations mp2_fops = {
   .owner = THIS_MODULE,
   .open = mp2_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release,
   .write = mp2_write
};

int __init mp1_init(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP1 MODULE LOADING\n");
    #endif

    /* section: initialization */
    mp2_dir = proc_mkdir("mp2", NULL);
    mp2_proc = proc_create("status", 0777, mp2_dir, &mp2_fops);

    printk(KERN_ALERT "MP1 MODULE LOADED\n");
    return 0;
}

void __exit mp1_exit(void)
{
    /* section: free global pointers */
	if (input_str != NULL)
	{
		printk(KERN_ALERT "input_str is not null, free it.\n");
		kfree(input_str);
		input_str = NULL;
	}

    /* section: remove target proc file */
	remove_proc_entry("status", mp2_dir);
	remove_proc_entry("mp2", NULL);

    printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);