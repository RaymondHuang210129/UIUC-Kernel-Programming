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

#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1

/* section: type definition */

struct pid_entry {
   long pid;
   unsigned long cpu_use;
   struct list_head ptrs;
};

/* section: function declaration */

static int mp1_show(struct seq_file * m, void * v);
static int mp1_open(struct inode *inode, struct file *file);
ssize_t mp1_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos);
void mp1_work_function(struct work_struct * work);
void timer_callback(unsigned long data);

/* section: variable initialization */

static struct proc_dir_entry* mp1_proc;
static struct proc_dir_entry* mp1_dir;
static char * input_str;

static struct list_head mp1_entries;
static struct timer_list mp1_timer;
static struct workqueue_struct * mp1_workqueue;

DEFINE_MUTEX(linked_list_mutex);

static const struct file_operations mp1_fops = {
   .owner = THIS_MODULE,
   .open = mp1_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release,
   .write = mp1_write
};

/* section: function definition */

static int mp1_show(struct seq_file * m, void * v)
{
   struct list_head *pos, *q;
   struct pid_entry *tmp;

   printk(KERN_ALERT "mp1_show printing.\n");
   mutex_lock(&linked_list_mutex);
   list_for_each_safe(pos, q, &mp1_entries)
   {
      tmp = list_entry(pos, struct pid_entry, ptrs);
      seq_printf(m, "%ld: %lu\n", tmp->pid, tmp->cpu_use);
   }
   mutex_unlock(&linked_list_mutex);

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
   new_entry->cpu_use = 0;
   INIT_LIST_HEAD(&(new_entry->ptrs));

   printk(KERN_ALERT "add new entry to list.\n");
   mutex_lock(&linked_list_mutex);
   list_add(&(new_entry->ptrs), &mp1_entries);
   mutex_unlock(&linked_list_mutex);

   return (ssize_t)size;
}

void mp1_work_function(struct work_struct * work) 
{
   struct list_head *pos, *q;
   struct pid_entry *tmp;
   unsigned long cpu_use;
   printk(KERN_ALERT "work function activated.\n");
   mutex_lock(&linked_list_mutex);
   list_for_each_safe(pos, q, &mp1_entries)
   {
      tmp = list_entry(pos, struct pid_entry, ptrs);
      if(get_cpu_use(tmp->pid, &cpu_use))
      {
         printk(KERN_ALERT "pid not exist: %ld.\n", tmp->pid);
         list_del(pos);
         kfree(tmp);
      }
      else 
      {
         tmp->cpu_use = cpu_use;
         printk(KERN_ALERT "update process id %ld's CPU usage: %lu", tmp->pid, tmp->cpu_use);
      }
   }
   mutex_unlock(&linked_list_mutex);
   kfree((void *)work);
}

void timer_callback(unsigned long data) {
   struct work_struct * new_work;
   new_work = kzalloc(sizeof(struct work_struct), GFP_KERNEL);
   mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));
   INIT_WORK(new_work, mp1_work_function);
   queue_work(mp1_workqueue, new_work);
}

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif

   /* section: initialization */
   input_str = NULL;

   /* section: create target proc file */
   mp1_dir = proc_mkdir("mp1", NULL);
   mp1_proc = proc_create("status", 0777, mp1_dir, &mp1_fops);

   /* section: create empty linked list */
   mp1_entries.next = &mp1_entries;
   mp1_entries.prev = &mp1_entries;

   /* section: setup timer */
   setup_timer(&mp1_timer, timer_callback, 0);
   mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));

   /* section: create workqueue */
   mp1_workqueue = create_workqueue("mp1_workqueue");

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

   /* section: delete workqueue */
   flush_workqueue(mp1_workqueue);
   destroy_workqueue(mp1_workqueue);

   /* section: delete timer */
   del_timer_sync(&mp1_timer);

   /* section: delete linked list */
   mutex_lock(&linked_list_mutex);
   list_for_each_safe(pos, q, &mp1_entries)
   {
      tmp = list_entry(pos, struct pid_entry, ptrs);
      printk(KERN_ALERT "freeing entry pid = %ld.\n", tmp->pid);
      list_del(pos);
      kfree(tmp);
   }
   mutex_unlock(&linked_list_mutex);

   /* section: free global pointers */
   if (input_str != NULL)
   {
      printk(KERN_ALERT "input_str is not null, free it.\n");
      kfree(input_str);
      input_str = NULL;
   }

   /* section: remove target proc file */
   remove_proc_entry("status", mp1_dir);
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
step 4 partially completed. copy_to_user is not used.
step 5 completed.
step 6 ~ 8 completed.
*/
