## CS423 Spring 2020 MP1
### Simple Kernel Module for Showing Processes' CPU Time 

#### Run program:

- Compile module:
    ```shell
    raymond@ubuntu:~/mp1-cputime$ make
    ```
- Install module:
    ```shell
    raymond@ubuntu:~/mp1-cputime$ sudo insmod ./mp1.ko
    ```
- Test user program:
    ```shell
    raymond@ubuntu:~/mp1-cputime$ ./userapp & ./userapp &
    [1] 14057
    [2] 14058
    raymond@ubuntu:~/mp1-cputime$ 14541: 3689
    14541: 3689
    14540: 3694
    14540: 3694
    
    [1]-  Done                    ./userapp
    [2]+  Done                    ./userapp
    ```
- Remove module
    ```shell
    raymond@ubuntu:~/mp1-cputime$ sudo rmmod mp1
    ```
    
#### Code explanation:
- Register init and exit function:
    ```c
    module_init(mp1_init);
    module_exit(mp1_exit);
    ```
- Init function:

    - Create a proc file:
        ```c
        int __init mp1_init(void)
        {
            ...
            mp1_dir = proc_mkdir("mp1", NULL);
            mp1_proc = proc_create("status", 0777, mp1_dir, &mp1_fops);
            ...
        }
        ```
    - Create a linked list instance to store registered processes:
        ```c
        static struct list_head mp1_entries;
        
        int __init mp1_init(void)
        {
            ...
            mp1_entries.next = &mp1_entries;
            mp1_entries.prev = &mp1_entries;
            ...
        }
        ```
    - Setup a timer and create workqueue with and work_struct instance:
        ```c
        static struct timer_list mp1_timer;
        static struct workqueue_struct * mp1_workqueue;
        struct work_struct * deferred_work;
        
        int __init mp1_init(void)
        {
            ...
            setup_timer(&mp1_timer, timer_callback, 0);
            mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));
            deferred_work = kzalloc(sizeof(struct work_struct), GFP_KERNEL);
            INIT_WORK(deferred_work, mp1_work_function);
            mp1_workqueue = create_workqueue("mp1_workqueue");
            ...
        }
        ```
- Proc file
    ```c
    static const struct file_operations mp1_fops = {
       .owner = THIS_MODULE,
       .open = mp1_open,
       .read = seq_read,
       .llseek = seq_lseek,
       .release = single_release,
       .write = mp1_write
    };
    ```

    - write proc file: register new process to the linked list
        ```c
        struct pid_entry {
            long pid;
            unsigned long cpu_use;
            struct list_head ptrs;
        };
        
        static char * input_str;
        
        ssize_t mp1_write(struct file * file, const char __user * ubuf, size_t size, loff_t * pos)
        {
            long pid = 0;
            ...
            input_str = kzalloc(size + 1, GFP_KERNEL);
            ...
            copy_from_user(input_str, ubuf, size)
            ...
            kstrtol(input_str, 0, &pid)
            ...
            new_entry = kzalloc(sizeof(struct pid_entry), GFP_KERNEL);
            ...
            new_entry->pid = pid;
            new_entry->cpu_use = 0;
            INIT_LIST_HEAD(&(new_entry->ptrs));
            ...
            mutex_lock(&linked_list_mutex);
            list_add(&(new_entry->ptrs), &mp1_entries);
            mutex_unlock(&linked_list_mutex);
        }
        ```
    - read proc file: iterate the linked list to print each registered process's CPU time
        ```c
        static int mp1_show(struct seq_file * m, void * v)
        {
            struct list_head *pos, *q;
            struct pid_entry *tmp;
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
        ```
- Timers and workqueue
    ```c
    #include "mp1_given.h"
    
    void mp1_work_function(struct work_struct * work) 
    {
        struct list_head *pos, *q;
        struct pid_entry *tmp;
        unsigned long cpu_use;
        ...
        mutex_lock(&linked_list_mutex);
        list_for_each_safe(pos, q, &mp1_entries)
        {
            tmp = list_entry(pos, struct pid_entry, ptrs);
            if(get_cpu_use(tmp->pid, &cpu_use))
            {
            ...
            list_del(pos);
            kfree(tmp);
            }
            else 
            {
                tmp->cpu_use = cpu_use;
                ...
            }
        }
        mutex_unlock(&linked_list_mutex);
    }
    
    void timer_callback(unsigned long data) 
    {
        ...
        mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));
        queue_work(mp1_workqueue, deferred_work);
    }
    ```
    note 1: `timer_callback()` is processed in interrupt context, must be as short as possible and avoid sleeping
    
    note 2: when iterating the linked list, each instance is either updated or deleted
