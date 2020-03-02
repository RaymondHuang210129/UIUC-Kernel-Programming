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
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1

/* section: type definition */

struct mp2_process_entry {
    struct list_head ptrs;
    struct task_struct * linux_task;
    struct timer_list wakeup_timer;
    unsigned long pid;
    unsigned long period;
    unsigned long comp_time;
};

/* section: variable initialization */

static struct proc_dir_entry * mp2_proc;
static struct proc_dir_entry * mp2_dir;
static char * input_str;

static struct kmem_cache * mp2_process_entries_slab = NULL;
static struct list_head mp2_process_entries;

DEFINE_MUTEX(process_list_mutex);

/* section: function definition */

static int mp2_show(struct seq_file * m, void * v)
{
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;

    /* section: iterate each linked list and print out cpu_use */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp2_process_entries) {
        tmp = list_entry(pos, struct mp2_process_entry, ptrs);
        seq_printf(m, "%ld %ld %ld\n", tmp->pid, tmp->period, tmp->comp_time);
    }
    mutex_unlock(&process_list_mutex);
    return 0;
}

static int mp2_open(struct inode *inode, struct file *file)
{
    return single_open(file, mp2_show, NULL);
}

static void timer_callback(unsigned long data)
{
    /* warning: in interrupt context */
    struct mp2_process_entry * process = (struct mp2_process_entry *)data;
    printk(KERN_ALERT "process %ld timer callback\n", process->pid);
    mod_timer(&process->wakeup_timer, jiffies + msecs_to_jiffies(process->period));
}

static int registration(unsigned long pid, unsigned long period, unsigned long comp_time)
{
    /* section: create new process node */
    struct mp2_process_entry * new_process = kmem_cache_alloc(mp2_process_entries_slab, GFP_KERNEL);
    if (!new_process) {
        printk(KERN_ALERT "kmem alloc error\n");
        return 1;
    }
    new_process->pid = pid;
    new_process->period = period;
    new_process->comp_time = comp_time;
    new_process->linux_task = NULL;
    new_process->linux_task = find_task_by_pid((unsigned int)pid);
    if (!new_process->linux_task)
    {
        printk(KERN_ALERT "dummy input\n");
    }
    setup_timer(&new_process->wakeup_timer, timer_callback, (unsigned long)new_process);
        /* note: the third parameter is the address of new_process for callback function to use */
    mod_timer(&new_process->wakeup_timer, jiffies + msecs_to_jiffies(period));
    INIT_LIST_HEAD(&new_process->ptrs);
    
    /* section: add into process list */
    mutex_lock(&process_list_mutex);
    list_add(&new_process->ptrs, &mp2_process_entries);
    mutex_unlock(&process_list_mutex);
    return 0;
}

static int yield(unsigned long pid)
{
    return 0;
}

static int deregistration(unsigned long pid)
{
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;

    /* section: delete the node belongs to pid */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp2_process_entries)
    {
        tmp = list_entry(pos, struct mp2_process_entry, ptrs);
        if (tmp->pid == pid)
        {
            /* section: delete timer, delete node, and break */
            printk(KERN_ALERT "delete timer of PID %ld", tmp->pid);
            del_timer_sync(&tmp->wakeup_timer);
            printk(KERN_ALERT "delete entry PID %ld", tmp->pid);
            list_del(pos);
            kmem_cache_free(mp2_process_entries_slab, tmp);
            mutex_unlock(&process_list_mutex);
            return 0;
        }
    }  
    mutex_unlock(&process_list_mutex);
    return 1;
}

static ssize_t mp2_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
{
    char * buffer;
    char * str_ptr;
    long pid = 0;
    long period = 0;
    long computation_time = 0;
    
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
    str_ptr = input_str;
    buffer = strsep(&str_ptr, ",");
    if (strlen(buffer) == 1 && strncmp(buffer, "R", 1) == 0) 
    {
        printk(KERN_ALERT "get R\n");

        /* section: get PID */
        buffer = strsep(&str_ptr, ",");
        if (kstrtol(buffer, 0, &pid))
        {
		    printk(KERN_ALERT "get invalid PID.\n");
		    return (ssize_t)size;
	    }
        printk(KERN_ALERT "PID: %ld\n", pid);

        /* section: get period */
        buffer = strsep(&str_ptr, ",");
        if (kstrtol(buffer, 0, &period))
        {
		    printk(KERN_ALERT "get invalid period.\n");
		    return (ssize_t)size;
	    }
        printk(KERN_ALERT "period: %ld\n", period);

        /* section: get computation time */
        if (kstrtol(str_ptr, 0, &computation_time))
        {
		    printk(KERN_ALERT "get invalid comp time.\n");
		    return (ssize_t)size;
	    }
        printk(KERN_ALERT "computation time: %ld\n", computation_time);
        str_ptr = NULL;

        /* section: registration */
        if (registration(pid, period, computation_time))
        {
            printk(KERN_ALERT "registration failed\n");
        }
    } 
    else if (strlen(buffer) >= 1 && strncmp(buffer, "Y", 1) == 0) 
    {
        printk(KERN_ALERT "get Y\n");

        /* section: get PID */
        if (kstrtol(str_ptr, 0, &pid))
        {
		    printk(KERN_ALERT "get invalid pid.\n");
		    return (ssize_t)size;
	    }
        printk(KERN_ALERT "PID: %ld\n", pid);
        str_ptr = NULL;
        if (yield(pid))
        {
            printk(KERN_ALERT "yield failed\n");
        }
    } 
    else if (strlen(buffer) >= 1 && strncmp(buffer, "D", 1) == 0) 
    {
        printk(KERN_ALERT "get D\n");

        /* section: get PID */
        if (kstrtol(str_ptr, 0, &pid))
        {
		    printk(KERN_ALERT "get invalid pid.\n");
		    return (ssize_t)size;
	    }
        printk(KERN_ALERT "PID: %ld\n", pid);
        str_ptr = NULL;

        /* section: deregistration */
        if(deregistration(pid))
        {
            printk(KERN_ALERT "deregistration failed\n");
        }
    } 
    else 
    {
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

int __init mp2_init(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP1 MODULE LOADING\n");
    #endif

    /* section: initialization */
    input_str = NULL;

    /* section: create target proc file */
    mp2_dir = proc_mkdir("mp2", NULL);
    mp2_proc = proc_create("status", 0777, mp2_dir, &mp2_fops);

    /* section: create slab */
    mp2_process_entries_slab = kmem_cache_create("mp2_process_struct slab", sizeof(struct mp2_process_entry), 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!mp2_process_entries_slab) return -ENOMEM;
    printk(KERN_ALERT "create slab successfully\n");

    /* section: create process linked list */
    mp2_process_entries.next = &mp2_process_entries;
    mp2_process_entries.prev = &mp2_process_entries;



    printk(KERN_ALERT "MP1 MODULE LOADED\n");
    return 0;
}

void __exit mp2_exit(void)
{
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;
    
    /* section: free global pointers */
	if (input_str != NULL)
	{
		printk(KERN_ALERT "input_str is not null, free it.\n");
		kfree(input_str);
		input_str = NULL;
	}

    /* section: stop all timers and deregistrate all processes */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp2_process_entries)
    {
        tmp = list_entry(pos, struct mp2_process_entry, ptrs);
        printk(KERN_ALERT "delete timer of PID %ld", tmp->pid);
        del_timer_sync(&tmp->wakeup_timer);
        printk(KERN_ALERT "delete entry PID %ld", tmp->pid);
        list_del(pos);
        kmem_cache_free(mp2_process_entries_slab, tmp);
    }
    mutex_unlock(&process_list_mutex);


    /* section: destroy slab */
    printk(KERN_ALERT "destroy slab\n");
    kmem_cache_destroy(mp2_process_entries_slab);

    /* section: remove target proc file */
	remove_proc_entry("status", mp2_dir);
	remove_proc_entry("mp2", NULL);

    printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);