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
#include <linux/cdev.h>

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
    unsigned long previous_jiffies;
};

struct mp3_statistic {
    unsigned long current_jiffies;
    unsigned long minor_fault_count;
    unsigned long major_fault_count;
    unsigned long utilization;
};

/* section: function delcaration */

static int mp3_show(struct seq_file * m, void * v);
static int mp3_proc_open(struct inode *inode, struct file *file);
static ssize_t mp3_proc_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos);
static int mp3_cdev_open(struct inode *inode, struct file *file);
static int mp3_cdev_release(struct inode *inode, struct file *file);
static int mp3_cdev_mmap(struct file * file, struct vm_area_struct * vma);
static void mp3_work_function(struct work_struct * work);
static void timer_callback(unsigned long data);
static int registration(unsigned long pid);
static int deregistration(unsigned long pid);

/* section: variable declaration & initialization */

static struct proc_dir_entry * mp3_proc;
static struct proc_dir_entry * mp3_dir;
static char * input_str;

static struct kmem_cache * mp3_process_entries_slab = NULL;
static char * profiler_buffer = NULL;
static char * profiler_buffer_offset = NULL;
static struct list_head mp3_process_entries;

DEFINE_MUTEX(process_list_mutex);

static struct timer_list * mp3_timer = NULL;
static struct work_struct * deferred_work;
static struct workqueue_struct * mp3_workqueue;

static struct cdev mp3_cdev;
static int cdev_major;

static const struct file_operations mp3_proc_fops = {
    .owner = THIS_MODULE,
    .open = mp3_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
    .write = mp3_proc_write
};

static const struct file_operations mp3_cdev_fops = {
    .owner = THIS_MODULE,
    .open = mp3_cdev_open,
    .release = mp3_cdev_release,
    .mmap = mp3_cdev_mmap
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

static int mp3_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, mp3_show, NULL);
}

static void mp3_work_function(struct work_struct * work) 
{
    struct list_head *pos, *q;
    struct mp3_process_entry *tmp;
    struct mp3_statistic statistic;
    unsigned long utime, stime;
    printk(KERN_ALERT "work function activated.\n");
    mutex_lock(&process_list_mutex);

    /* section: print out each process's minor page fault and major page fault */
    list_for_each_safe(pos, q, &mp3_process_entries)
    {
        tmp = list_entry(pos, struct mp3_process_entry, ptrs);
        if (get_cpu_use((int)tmp->pid, &statistic.minor_fault_count, &statistic.major_fault_count, &utime, &stime))
        {
            printk(KERN_ALERT "Process %ld DNE. Is it dummy?\n", tmp->pid);
            list_del(pos);

            /* section: delete workqueue (if process list is empty) */
            if (list_empty(&mp3_process_entries))
            {
                flush_workqueue(mp3_workqueue);
                destroy_workqueue(mp3_workqueue);
                kfree(deferred_work);
                del_timer_sync(mp3_timer);
                kfree(mp3_timer);
                mp3_timer = NULL;
                profiler_buffer_offset = NULL;
            }
        }
        else
        {
            statistic.current_jiffies = jiffies;
            //printk(KERN_ALERT "utime+stime %lu delta jiffies %lu", (utime + stime), (jiffies - tmp->previous_jiffies));
            statistic.utilization = (utime + stime) * 100 / (jiffies - tmp->previous_jiffies);
            tmp->previous_jiffies = jiffies;
            if (profiler_buffer_offset != NULL && profiler_buffer_offset != profiler_buffer + 128 * PAGE_SIZE)
            {
                *(struct mp3_statistic *)profiler_buffer_offset = statistic;
                profiler_buffer_offset += sizeof(struct mp3_statistic);
            }
            else
            {
                printk(KERN_ALERT "buffer is full.\n");
            }
            
        }
        

    }
    mutex_unlock(&process_list_mutex);
}

static void timer_callback(unsigned long data) 
{
    /* section: generate new work */
    /* note: this function is processed in interrupt context */
    mod_timer(mp3_timer, jiffies + msecs_to_jiffies(50));
    queue_work(mp3_workqueue, deferred_work);
}

static int registration(unsigned long pid)
{   
    struct mp3_process_entry * new_process;
    int i;

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
        for (i = 0; i < PAGE_SIZE * 128; i += sizeof(unsigned long))
        {
            *(unsigned long *)(profiler_buffer + i) = -1;
        }
        profiler_buffer_offset = profiler_buffer;
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
                profiler_buffer_offset = NULL;
            }

            mutex_unlock(&process_list_mutex);

            return 0;
        }
    }  
    mutex_unlock(&process_list_mutex);

    return 1;
}

static ssize_t mp3_proc_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
{
    long pid = 0;
    char m; // message type, "R", "Y", or "D"

    if (input_str != NULL)
    {
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

    switch (m)
    {
    case 'R':
        if (registration(pid))
        {
            printk(KERN_ALERT "registration failed\n");
        }
        break;
    case 'U':
        if (deregistration(pid))
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

static int mp3_cdev_open(struct inode *inode, struct file *file)
{
    try_module_get(THIS_MODULE);
    return 0;
}

static int mp3_cdev_release(struct inode *inode, struct file *file)
{
    module_put(THIS_MODULE);
    return 0;
}

static int mp3_cdev_mmap(struct file * file, struct vm_area_struct * vma)
{
    unsigned long pfn, i;
    /* section: mapping the valloc memory to userspace */
    /* note: memory allocated by valloc can be discontinuous, each page should be mapped respectively */
    for (i = 0; i < 128 * PAGE_SIZE; i += PAGE_SIZE)
    {
        pfn = vmalloc_to_pfn(profiler_buffer + i);
        if (remap_pfn_range(vma, (vma->vm_start + i), pfn, PAGE_SIZE, vma->vm_page_prot))
        {
            printk(KERN_ALERT "error remapping %ld", i);
        }
    }
    return 0;
}

int __init mp3_init(void)
{
    int i;
    #ifdef DEBUG
    printk(KERN_ALERT "MP3 MODULE LOADING\n");
    #endif

    /* section: initialization */
    input_str = NULL;

    /* section: create target proc file */
    mp3_dir = proc_mkdir("mp3", NULL);
    mp3_proc = proc_create("status", 0777, mp3_dir, &mp3_proc_fops);

    /* section: create slab */
    mp3_process_entries_slab = kmem_cache_create("mp3_process_struct slab", sizeof(struct mp3_process_entry), 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!mp3_process_entries_slab) return -ENOMEM;

    /* section: create process linked list */
    mp3_process_entries.next = &mp3_process_entries;
    mp3_process_entries.prev = &mp3_process_entries;

    /* section: create memory buffer */
    profiler_buffer = vmalloc(PAGE_SIZE * 128);
    /* note: prevent each page from  */
    for (i = 0; i < 128 * PAGE_SIZE; i += PAGE_SIZE)
    {
        SetPageReserved(vmalloc_to_page(profiler_buffer + i));
    }

    /* section: create character decvice */
    cdev_major = register_chrdev(0, "node", &mp3_cdev_fops);

    printk(KERN_ALERT "MP3 MODULE LOADED\n");
    return 0;
}

void __exit mp3_exit(void)
{
    struct list_head *pos, *q;
    struct mp3_process_entry *tmp;
    int i;
    
    /* section: free global pointers */
	if (input_str != NULL)
	{
		printk(KERN_ALERT "input_str is not null, free it.\n");
		kfree(input_str);
		input_str = NULL;
	}

    /* section: delete character device */
    unregister_chrdev(cdev_major, "node");

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
    for (i = 0; i < 128 * PAGE_SIZE; i += PAGE_SIZE)
    {
        ClearPageReserved(vmalloc_to_page(profiler_buffer + i));
    }
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