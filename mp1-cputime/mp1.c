#define LINUX

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1

static struct proc_dir_entry* mp1_proc;
static struct proc_dir_entry* mp1_dir;
static char * input_str;

struct pid_entry {
   long pid;
   struct list_head ptrs;
};

static struct list_head mp1_entries;

static int mp1_show(struct seq_file * m, void * v)
{
   seq_printf(m, "mp1_show printing.\n");
   return 0;
}

static int mp1_open(struct inode *inode, struct file *file)
{
   return single_open(file, mp1_show, NULL);
}

ssize_t mp1_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
{
   long pid = 0;
   struct pid_entry * new_entry = NULL;

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

   if (kstrtol(input_str, 0, &pid))
   {
      printk(KERN_ALERT "get invalid PID.\n");
      return (ssize_t)size;
   } else {
      printk(KERN_ALERT "get PID: %ld.\n", pid);
   }

   printk(KERN_ALERT "creating new entry.\n");
   new_entry = kzalloc(sizeof(struct pid_entry), GFP_KERNEL);
   if (!new_entry)
   {
      printk(KERN_ALERT "unable to alloc new_entry.\n");
      return (ssize_t)size;
   }
   new_entry->pid = pid;
   INIT_LIST_HEAD(&(new_entry->ptrs));
   printk(KERN_ALERT "add new entry to list.\n");
   list_add(&(new_entry->ptrs), &mp1_entries);

   return (ssize_t)size;
}

static const struct file_operations mp1_fops = {
   .owner = THIS_MODULE,
   .open = mp1_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release,
   .write = mp1_write
};

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif

   // Insert your code here ...

   // section: initialization
   printk(KERN_ALERT "input_str set to NULL.\n");
   input_str = NULL;

   printk(KERN_ALERT "adding proc directory \"mp1\"\n");
   mp1_dir = proc_mkdir("mp1", NULL);
   
   printk(KERN_ALERT "adding proc file \"status\"\n");
   mp1_proc = proc_create("status", 0777, mp1_dir, &mp1_fops);

   printk(KERN_ALERT "creating empty linked list.\n");
   mp1_entries.next = &mp1_entries;
   mp1_entries.prev = &mp1_entries;


   
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   struct list_head *pos, *q;
   struct pid_entry *tmp;

   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif

   // Insert your code here ...

   printk(KERN_ALERT "freeing the whole linked list.\n");
   list_for_each_safe(pos, q, &mp1_entries)
   {
      tmp = list_entry(pos, struct pid_entry, ptrs);
      printk(KERN_ALERT "freeing entry pid = %ld.\n", tmp->pid);
      list_del(pos);
      kfree(tmp);
   }


   if (input_str != NULL)
   {
      printk(KERN_ALERT "input_str is not null, free it.\n");
      kfree(input_str);
      input_str = NULL;
   }

   printk(KERN_ALERT "remove proc file \"status\".\n");
   remove_proc_entry("status", mp1_dir);

   printk(KERN_ALERT "remove proc directory \"mp1\".\n");
   remove_proc_entry("mp1", NULL);




   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);

/*
step 1 completed.
step 2 completed.
step 3 completed.
*/
