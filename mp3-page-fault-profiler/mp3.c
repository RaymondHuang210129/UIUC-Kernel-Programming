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
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include "mp3_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tse-Jui Huang");
MODULE_DESCRIPTION("CS-423 MP3");

#define DEBUG 1

/* section: type definition */

struct mp3_process_entry {
    struct list_head ptrs;
    struct task_struct * linux_task;
    //struct timer_list wakeup_timer;
    //int timer_postpone;
    unsigned long pid;
    //unsigned long period;
    //unsigned long comp_time;
    long state;
    //unsigned long original_arrived_time;
    unsigned long utilization;
    unsigned long major_fault_count;
    unsigned long minor_fault_count;
};

struct mp3_statistic {
    unsigned long current_jiffies;
    unsigned long minor_fault_count;
    unsigned long major_fault_count;
    unsigned long utilization;
};

/* section: function delcaration */

static int mp3_show(struct seq_file * m, void * v);
static int mp3_open(struct inode *inode, struct file *file);
static ssize_t mp3_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos);
static void mp3_work_function(struct work_struct * work);
static void timer_callback(unsigned long data);
static int registration(unsigned long pid);
static int deregistration(unsigned long pid);

/* section: variable declaration & initialization */

static struct proc_dir_entry * mp3_proc;
static struct proc_dir_entry * mp3_dir;
static char * input_str;

static struct kmem_cache * mp3_process_entries_slab = NULL;
static struct mp3_statistic * profiler_buffer = NULL;
static struct list_head mp3_process_entries;

DEFINE_MUTEX(process_list_mutex);

static struct timer_list * mp3_timer = NULL;
struct work_struct * deferred_work;
static struct workqueue_struct * mp3_workqueue;

static const struct file_operations mp3_fops = {
    .owner = THIS_MODULE,
    .open = mp3_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
    .write = mp3_write
};

/* section: function definition */

static int mp3_show(struct seq_file * m, void * v)
{
    struct list_head *pos, *q;
    struct mp3_process_entry *tmp;

    /* section: iterate each linked list and print out cpu_use */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp3_process_entries) {
        tmp = list_entry(pos, struct mp3_process_entry, ptrs);
        seq_printf(m, "%ld\n", tmp->pid);
    }
    mutex_unlock(&process_list_mutex);
    return 0;
}

static int mp3_open(struct inode *inode, struct file *file)
{
    return single_open(file, mp3_show, NULL);
}

static void mp3_work_function(struct work_struct * work) 
{
    struct list_head *pos, *q;
    struct mp3_process_entry *tmp;
    printk(KERN_ALERT "work function activated.\n");
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp3_process_entries)
    {
        tmp = list_entry(pos, struct mp3_process_entry, ptrs);
    }
    mutex_unlock(&process_list_mutex);
}

static void timer_callback(unsigned long data) {
    /* section: generate new work */
    /* note: this function is processed in interrupt context */
    mod_timer(mp3_timer, jiffies + msecs_to_jiffies(50));
    queue_work(mp3_workqueue, deferred_work);
}

static int registration(unsigned long pid)
{   
    struct mp3_process_entry * new_process;

    /* section: create new process node */
    new_process = kmem_cache_alloc(mp3_process_entries_slab, GFP_KERNEL);
    if (!new_process) {
        printk(KERN_ALERT "kmem alloc error\n");
        return 1;
    }
    new_process->pid = pid;
    new_process->linux_task = NULL;
    new_process->linux_task = find_task_by_pid((unsigned int)pid);
    new_process->state = TASK_RUNNING;
    new_process->major_fault_count = 0;
    new_process->minor_fault_count = 0;
    new_process->utilization = 0;
    if (!new_process->linux_task)
    {
        printk(KERN_ALERT "dummy input\n");
    }
    INIT_LIST_HEAD(&new_process->ptrs);

    /* section: add into process list */
    mutex_lock(&process_list_mutex);
    list_add(&new_process->ptrs, &mp3_process_entries);

    /* section: create timer and workqueue (if not) */
    if (mp3_timer == NULL)
    {
        mp3_timer = kzalloc(sizeof(struct timer_list), GFP_KERNEL);
        setup_timer(mp3_timer, timer_callback, 0);
        mod_timer(mp3_timer, jiffies + msecs_to_jiffies(50));
        deferred_work = kzalloc(sizeof(struct work_struct), GFP_KERNEL);
        INIT_WORK(deferred_work, mp3_work_function);
        mp3_workqueue = create_workqueue("mp3_workqueue");
    }

    mutex_unlock(&process_list_mutex);

    return 0;
}

static int deregistration(unsigned long pid)
{
    struct list_head *pos, *q;
    struct mp3_process_entry *tmp;

    /* section: delete the node belongs to pid */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp3_process_entries)
    {
        tmp = list_entry(pos, struct mp3_process_entry, ptrs);
        if (tmp->pid == pid)
        {
            /* section: delete timer, delete node, and break */
            //printk(KERN_ALERT "drgst: %ld: delete entry\n", tmp->pid);
            list_del(pos);
            kmem_cache_free(mp3_process_entries_slab, tmp);

            /* section: delete workqueue (if process list is empty) */
            if (list_empty(&mp3_process_entries))
            {
                flush_workqueue(mp3_workqueue);
                destroy_workqueue(mp3_workqueue);
                kfree(deferred_work);
                del_timer_sync(mp3_timer);
                kfree(mp3_timer);
                mp3_timer = NULL;
            }

            mutex_unlock(&process_list_mutex);

            return 0;
        }
    }  
    mutex_unlock(&process_list_mutex);

    return 1;
}

static ssize_t mp3_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
{
    long pid = 0;
    char m; // message type, "R", "Y", or "D"
    
    if (input_str != NULL) {
		kfree(input_str);
		input_str = NULL;
	}

	input_str = kzalloc(size + 1, GFP_KERNEL);
    if (input_str == NULL)
	{
		printk(KERN_ALERT "unable to kzalloc.\n");
		return -ENOMEM;
	}

	if (copy_from_user(input_str, ubuf, size))
	{
		printk(KERN_ALERT "copy from user error.\n");
		return EFAULT;
	}

    /* section: identify command */
    sscanf(input_str, "%c%*[,] %lu", &m, &pid);

    switch(m){
    case 'R':
        if (registration(pid))
        {
            printk(KERN_ALERT "registration failed\n");
        }
        break;
    case 'U':
        if(deregistration(pid))
        {
            printk(KERN_ALERT "deregistration failed\n");
        }
        break;
    default:
        printk(KERN_ALERT "input unknown command\n");
        break;
    }
    return (ssize_t)size;
}

int __init mp3_init(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP3 MODULE LOADING\n");
    #endif

    /* section: initialization */
    input_str = NULL;

    /* section: create target proc file */
    mp3_dir = proc_mkdir("mp3", NULL);
    mp3_proc = proc_create("status", 0777, mp3_dir, &mp3_fops);

    /* section: create slab */
    mp3_process_entries_slab = kmem_cache_create("mp3_process_struct slab", sizeof(struct mp3_process_entry), 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!mp3_process_entries_slab) return -ENOMEM;

    /* section: create process linked list */
    mp3_process_entries.next = &mp3_process_entries;
    mp3_process_entries.prev = &mp3_process_entries;

    /*section: create memory buffer */
    profiler_buffer = vmalloc(sizeof(struct mp3_statistic) * 12000);
    //SetPageReserved()

    printk(KERN_ALERT "MP3 MODULE LOADED\n");
    return 0;
}

void __exit mp3_exit(void)
{
    struct list_head *pos, *q;
    struct mp3_process_entry *tmp;
    
    /* section: free global pointers */
	if (input_str != NULL)
	{
		printk(KERN_ALERT "input_str is not null, free it.\n");
		kfree(input_str);
		input_str = NULL;
	}

    /* section: stop workqueue and deregistrate all processes */
    mutex_lock(&process_list_mutex);
    if (mp3_timer != NULL)
    {
        flush_workqueue(mp3_workqueue);
        destroy_workqueue(mp3_workqueue);
        kfree(deferred_work);
        del_timer_sync(mp3_timer);
        kfree(mp3_timer);
        mp3_timer = NULL;
    }
    list_for_each_safe(pos, q, &mp3_process_entries)
    {
        tmp = list_entry(pos, struct mp3_process_entry, ptrs);
        printk(KERN_ALERT "delete entry PID %ld", tmp->pid);
        list_del(pos);
    }
    mutex_unlock(&process_list_mutex);

    /* section: destroy slab */
    printk(KERN_ALERT "destroy slab\n");
    kmem_cache_destroy(mp3_process_entries_slab);

    /* section: free memory buffer */
    vfree(profiler_buffer);

    /* section: remove target proc file */
    printk(KERN_ALERT "removing 'status'");
	remove_proc_entry("status", mp3_dir);
    printk(KERN_ALERT "removing 'mp3'");
	remove_proc_entry("mp3", NULL);

    printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);