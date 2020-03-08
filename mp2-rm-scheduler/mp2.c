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
    int timer_postpone;
    unsigned long pid;
    unsigned long period;
    unsigned long comp_time;
    long state;
    unsigned long original_arrived_time;
};

/* section: function delcaration */

static int mp2_show(struct seq_file * m, void * v);
static int mp2_open(struct inode *inode, struct file *file);
static void timer_callback(unsigned long data);
static int admission_control(unsigned long period, unsigned long comp_time);
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
static struct timespec64 current_time;

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

    /** note: timer_postpone is used when two timers arrive simultaneously.
     * if two call_back functions are trigered too close, dispatch thread
     * will only process once. To avoid this situation, if wake_up_process()
     * returns 1, the another timer_callback is needed, instead of spinning
     * this function call. This is because that timer_callback is in interrupt
     * mode, spinning will cause starvation to all the other processes. **/
    if (process->timer_postpone == 1)
    {
        /* note: postponed timer_callback runs here */
        if (!wake_up_process(dispatching_thread))
        {
            /* note: timer should be postponed again for a while */
            mod_timer(&process->wakeup_timer, jiffies + msecs_to_jiffies(2));
        }
        else
        {
            process->timer_postpone = 0;
            printk(KERN_ALERT "tmcbk: %ld: postponed timer wake up dispatch thread\n", process->pid);
        }
        
    } 
    else
    {
        /* note: the original timer set by yield() */
        printk(KERN_ALERT "tmcbk: %ld: execute timer callback\n", process->pid);
        process->state = TASK_RUNNING;
        if (!wake_up_process(dispatching_thread))
        {
            /* note: timer should be postponed for a while */
            printk(KERN_ALERT "tmcbk: %ld: dispatch thread is running, postpone 2 ms.\n", process->pid);
            process->timer_postpone = 1;
            mod_timer(&process->wakeup_timer, jiffies + msecs_to_jiffies(2));
        }
        else
        {
            printk(KERN_ALERT "tmcbk: %ld: timer wake up dispatch thread\n", process->pid);
        }
        
    }
}

static int admission_control(unsigned long period, unsigned long comp_time)
{
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;
    unsigned long total_workload = 0;

    /* section: calculate the total workload */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp2_process_entries)
    {
        tmp = list_entry(pos, struct mp2_process_entry, ptrs);
        total_workload += (tmp->comp_time * 1000) / tmp->period;
    }
    mutex_unlock(&process_list_mutex);
    total_workload += (comp_time * 1000) / period;
    if (total_workload > 693)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

static int registration(unsigned long pid, unsigned long period, unsigned long comp_time)
{   
    struct mp2_process_entry * new_process;

    /* section: admission control */
    if (admission_control(period, comp_time))
    {
        printk(KERN_ALERT "prohibited by admission control.\n");
        return 1;
    }

    /* section: create new process node */
    new_process = kmem_cache_alloc(mp2_process_entries_slab, GFP_KERNEL);
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
    new_process->original_arrived_time = jiffies;
    if (!new_process->linux_task)
    {
        printk(KERN_ALERT "dummy input\n");
    }
    /* note: the third parameter is the address of struct mp2_process_entry new_process for callback function to use */
    setup_timer(&new_process->wakeup_timer, timer_callback, (unsigned long)new_process);
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
    struct list_head *pos, *q;
    struct mp2_process_entry *tmp;

    /* section: modify process data of pid */
    mutex_lock(&process_list_mutex);
    list_for_each_safe(pos, q, &mp2_process_entries)
    {
        tmp = list_entry(pos, struct mp2_process_entry, ptrs);
        if (tmp->pid == pid)
        {
            /* note: check if the time has passed or not, reschedule (postpone) or set normally */
            if (jiffies < tmp->original_arrived_time + msecs_to_jiffies(tmp->period))
            {
                mod_timer(&tmp->wakeup_timer, tmp->original_arrived_time + msecs_to_jiffies(tmp->period));
                tmp->original_arrived_time = tmp->original_arrived_time + msecs_to_jiffies(tmp->period);
                tmp->timer_postpone = 0; /* note: will be used in timer_callback */
            }
            else
            {
                printk(KERN_ALERT "yield: %ld: deadline has passed, reschedule.", pid);
                mod_timer(&tmp->wakeup_timer, jiffies + msecs_to_jiffies(tmp->period));
                tmp->original_arrived_time = jiffies + msecs_to_jiffies(tmp->period);
                tmp->timer_postpone = 0;
            }
            tmp->state = TASK_INTERRUPTIBLE;
            set_task_state(tmp->linux_task, TASK_INTERRUPTIBLE);
            mutex_unlock(&process_list_mutex);

            goto success;
        }
    }
    mutex_unlock(&process_list_mutex);
    return -1;

success:
    if (running_process != NULL && running_process->pid == pid)
    {
        running_process = NULL;
        if (wake_up_process(dispatching_thread))
        {
            //printk(KERN_ALERT"yield: %ld: dispatch is already running\n", pid);
        }
        else
        {
            printk(KERN_ALERT"yield: %ld: wake up dispatch success\n", pid);
        }
    }
    else
    {
        //printk(KERN_ALERT "yield: %ld: first call yield", pid);
    }

    ktime_get_ts64(&current_time);
    printk(KERN_ALERT "yield: %ld: sleep process pid %lu, time=%ld\n", pid, tmp->pid, current_time.tv_sec);
    schedule();
    ktime_get_ts64(&current_time);
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
            printk(KERN_ALERT "drgst: %ld: delete timer\n", tmp->pid);
            del_timer_sync(&tmp->wakeup_timer);
            //printk(KERN_ALERT "drgst: %ld: delete entry\n", tmp->pid);
            list_del(pos);
            kmem_cache_free(mp2_process_entries_slab, tmp);
            mutex_unlock(&process_list_mutex);
            while(!wake_up_process(dispatching_thread));
            return 0;
        }
    }  
    mutex_unlock(&process_list_mutex);
    return 1;
}

static ssize_t mp2_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
{
    long pid = 0;
    long period = 0;
    long computation_time = 0;
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

    /* section: run 1 iteration per wake */
    while(!kthread_should_stop())
    {
        printk(KERN_ALERT "dspch: activated\n");
        /* note: thread is awaked */

        /* section: find the process which has largest priority */
        shortest_period = ULONG_MAX;
        highest_task = NULL;

        mutex_lock(&process_list_mutex);
        list_for_each_safe(pos, q, &mp2_process_entries)
        {
            tmp = list_entry(pos, struct mp2_process_entry, ptrs);
            if (tmp->state == TASK_RUNNING)
            {
                /* section: sellect a highest priority process from all runnable processes */
                if (tmp->period < shortest_period)
                {
                    shortest_period = tmp->period;
                    highest_task = tmp;
                }
            }
        }
        mutex_unlock(&process_list_mutex);

        /* section: sleep the running process (if any) */
        if (running_process != NULL && find_task_by_pid((unsigned int)running_process->pid) != NULL)
        {
            sparam_sleep.sched_priority = 0;
            sched_setscheduler(running_process->linux_task, SCHED_NORMAL, &sparam_sleep);
            printk(KERN_ALERT "dspch: stopping process pid %ld\n", running_process->pid);
            set_task_state(running_process->linux_task, TASK_INTERRUPTIBLE);
            running_process = NULL;
        }
        /* section: wake up the ready process (if any) */
        if (highest_task != NULL)
        {
            wake_up_process(highest_task->linux_task);
            sparam_wake.sched_priority = 99;
            sched_setscheduler(highest_task->linux_task, SCHED_FIFO, &sparam_wake);
            running_process = highest_task;
            printk(KERN_ALERT "dspch: waking process pid %lu\n", highest_task->pid);
        }

        set_current_state(TASK_INTERRUPTIBLE);
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

    printk(KERN_ALERT "MP2 MODULE LOADED\n");
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