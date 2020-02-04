#define LINUX

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1

static struct proc_dir_entry* mp1_proc;
static struct proc_dir_entry* mp1_dir;

static int mp1_show(struct seq_file *m, void *v)
{
   seq_printf(m, "this is proc file printing.");
   return 0;
}

static int mp1_open(struct inode *inode, struct file *file)
{
   return single_open(file, mp1_show, NULL);
}

static const struct file_operations mp1_fops = {
   .owner = THIS_MODULE,
   .open = mp1_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release
};

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif

   // Insert your code here ...
   printk(KERN_ALERT "adding proc directory \"mp1\"\n");
   mp1_dir = proc_mkdir("mp1", NULL);
   printk(KERN_ALERT "adding proc file \"status\"\n");
   mp1_proc = proc_create("status", 0, mp1_dir, &mp1_fops);

   
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif

   // Insert your code here ...
   printk(KERN_ALERT "remove proc file \"status\".\n");
   remove_proc_entry("status", mp1_dir);
   printk(KERN_ALERT "remove proc directory \"mp1\".\n");
   remove_proc_entry("mp1", NULL);


   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
