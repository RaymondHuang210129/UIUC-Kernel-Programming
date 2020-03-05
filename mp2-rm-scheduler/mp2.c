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
    long state;
    unsigned long timer_arrive_time;
};

/* section: function delcaration */

static int mp2_show(struct seq_file * m, void * v);
static int mp2_open(struct inode *inode, struct file *file);
static void timer_callback(unsigned long data);
static int registration(unsigned long pid, unsigned long period, unsigned long comp_time);
static int yield_cpu(unsigned long pid);
static int deregistration(unsigned long pid);
static ssize_t mp2_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos);
static int dispatching_func(void * data);

/* section: variable declaration & initialization */

static struct proc_dir_entry * mp2_proc;
static struct proc_dir_entry * mp2_dir;
static char * input_str;

static struct kmem_cache * mp2_process_entries_slab = NULL;
static struct list_head mp2_process_entries;

DEFINE_MUTEX(process_list_mutex);

static const struct file_operations mp2_fops = {
    .owner = THIS_MODULE,
    .open = mp2_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
    .write = mp2_write
};

static struct task_struct * dispatching_thread;
static struct mp2_process_entry * running_process = NULL;

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
    //struct sched_param sparam;

    struct mp2_process_entry * process = (struct mp2_process_entry *)data;
    printk(KERN_ALERT "process %ld timer callback\n", process->pid);
    //mod_timer(&process->wakeup_timer, jiffies + msecs_to_jiffies(process->period));
    //set_task_state(process->linux_task, TASK_RUNNING);
    process->state = TASK_RUNNING;
    process->timer_arrive_time = jiffies;
    //sparam.sched_priority = 0;
    //sched_setscheduler(process->linux_task, SCHED_NORMAL, &sparam);
    if (!wake_up_process(dispatching_thread))
    {
        printk(KERN_ALERT "already running");
    }
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
    new_process->state = TASK_RUNNING;
    new_process->timer_arrive_time = jiffies;
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

static int yield_cpu(unsigned long pid)
{
    //struct task_struct * yield_process = find_task_by_pid((unsigned int) pid);
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;
    list_for_each_safe(pos, q, &mp2_process_entries)
    {
        tmp = list_entry(pos, struct mp2_process_entry, ptrs);
        if (tmp->pid == pid)
        {
            mutex_lock(&process_list_mutex);
            if (jiffies < tmp->timer_arrive_time + msecs_to_jiffies(tmp->period))
            {
                mod_timer(&tmp->wakeup_timer, tmp->timer_arrive_time + msecs_to_jiffies(tmp->period));
            }
            else
            {
                printk(KERN_ALERT "deadline has passed.");
            }
            
            if (running_process != NULL && running_process->pid == pid)
            {
                printk(KERN_ALERT "yield again\n");
            } 
            else
            {
                printk(KERN_ALERT "first call yield");
            }
            
            mutex_unlock(&process_list_mutex);
            running_process = NULL;
            goto success;
        }
    }
    return -1;

success:
    /* section: put the process into sleep and wake up dispatching thread */
    tmp->state = TASK_IDLE;
    set_task_state(tmp->linux_task, TASK_IDLE);
    if (tmp->linux_task->state == TASK_IDLE)
    {
        printk(KERN_ALERT"set sleep success\n");
    }
    else
    {
        printk(KERN_ALERT"sleep failed\n");
        return -1;
    }
    schedule();
    if (!wake_up_process(dispatching_thread))
    {
        printk(KERN_ALERT "already running");
    }
    printk(KERN_ALERT "sleep process pid %lu\n", tmp->pid);
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
            printk(KERN_ALERT "delete timer of PID %ld\n", tmp->pid);
            del_timer_sync(&tmp->wakeup_timer);
            printk(KERN_ALERT "delete entry PID %ld\n", tmp->pid);
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
    //char * buffer;
    //char * str_ptr;
    long pid = 0;
    long period = 0;
    long computation_time = 0;
    char m; // message type, "R", "Y", or "D"
    
    if (input_str != NULL) {
		//printk(KERN_ALERT "input_str is not null.\n");
		kfree(input_str);
		input_str = NULL;
	} else {
		//printk(KERN_ALERT "input_str is null.\n");
	}

    //printk(KERN_ALERT "size passed from parameter: %lu\n", size);
	input_str = kzalloc(size + 1, GFP_KERNEL);
    if (input_str == NULL)
	{
		printk(KERN_ALERT "unable to kzalloc.\n");
		return -ENOMEM;
	}

    //printk(KERN_ALERT "copy from user.\n");
	if (copy_from_user(input_str, ubuf, size))
	{
		printk(KERN_ALERT "copy from user error.\n");
		return EFAULT;
	}

    /* section: identify command */
    sscanf(input_str, "%c%*[,] %lu%*[,] %lu%*[,] %lu", &m, &pid, &period, &computation_time);

    switch(m){
    case 'R':
        if (registration(pid, period, computation_time))
        {
            printk(KERN_ALERT "registration failed\n");
        }
        break;
    case 'Y':
        if (yield_cpu(pid))
        {
            printk(KERN_ALERT "yield failed\n");
        }
        break;
    case 'D':
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

static int dispatching_func(void * data)
{
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;
    long shortest_period;
    struct mp2_process_entry * highest_task;
    struct sched_param sparam_sleep, sparam_wake;
    
    set_current_state(TASK_INTERRUPTIBLE);
    schedule();
    while(!kthread_should_stop())
    {
        printk(KERN_ALERT "dispatching thread: activated\n");
        /* note: thread is awaked */
        printk(KERN_ALERT "dispatching thread: current running TLB addr: %p", running_process);

        /* section: find the process which has largest priority */
        shortest_period = ULONG_MAX;
        highest_task = NULL;

        mutex_lock(&process_list_mutex);

        list_for_each_safe(pos, q, &mp2_process_entries)
        {
            tmp = list_entry(pos, struct mp2_process_entry, ptrs);
            printk(KERN_ALERT "dispatching thread: process pid %lu state %ld\n", tmp->pid, tmp->state);
            if (tmp->state == TASK_RUNNING)
            {
                printk(KERN_ALERT "dispatching thread: process pid %lu can be run\n", tmp->pid);
                if (tmp->period < shortest_period)
                {
                    shortest_period = tmp->period;
                    highest_task = tmp;
                }
            }
        }

        /* section: sleep the running process (if any) */
        if (running_process != NULL)
        {
            sparam_sleep.sched_priority = 0;
            sched_setscheduler(running_process->linux_task, SCHED_NORMAL, &sparam_sleep);
            printk(KERN_ALERT "stopping process pid %ld\n", running_process->pid);
        }
        /* section: wake up the ready process (if any) */
        if (highest_task != NULL)
        {
            wake_up_process(highest_task->linux_task);
            sparam_wake.sched_priority = 99;
            if(sched_setscheduler(highest_task->linux_task, SCHED_FIFO, &sparam_wake) == -1)
            {
                printk("error\n");
            }
            schedule();
            running_process = highest_task;
            printk(KERN_ALERT "dispatching thread: waking process pid %lu\n", highest_task->pid);
        }

        mutex_unlock(&process_list_mutex);

        set_current_state(TASK_INTERRUPTIBLE);
        printk(KERN_ALERT "dispatch thread: sleep now\n");
        schedule();        
    }
    return 0;
}

int __init mp2_init(void)
{
    #ifdef DEBUG
    //printk(KERN_ALERT "MP1 MODULE LOADING\n");
    #endif

    /* section: initialization */
    input_str = NULL;

    /* section: create target proc file */
    mp2_dir = proc_mkdir("mp2", NULL);
    mp2_proc = proc_create("status", 0777, mp2_dir, &mp2_fops);

    /* section: create slab */
    mp2_process_entries_slab = kmem_cache_create("mp2_process_struct slab", sizeof(struct mp2_process_entry), 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!mp2_process_entries_slab) return -ENOMEM;
    //printk(KERN_ALERT "create slab successfully\n");

    /* section: create process linked list */
    mp2_process_entries.next = &mp2_process_entries;
    mp2_process_entries.prev = &mp2_process_entries;

    /* section: create kernel thread to conduct context switch */
    dispatching_thread = kthread_run(dispatching_func, NULL, "context switch thread");

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

    /* section: delete context switch thread */
    kthread_stop(dispatching_thread);

    /* section: destroy slab */
    printk(KERN_ALERT "destroy slab\n");
    kmem_cache_destroy(mp2_process_entries_slab);

    /* section: remove target proc file */
    printk(KERN_ALERT "removing 'status'");
	remove_proc_entry("status", mp2_dir);
    printk(KERN_ALERT "removing 'mp2'");
	remove_proc_entry("mp2", NULL);

    printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);