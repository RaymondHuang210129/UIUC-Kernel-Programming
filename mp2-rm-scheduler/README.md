# CS423 Spring 2020 MP2
### Rate-Monotonic CPU scheduling module

#### Run program:

- Compile module:
    ```shell
    raymond@ubuntu:~/mp2-rm-scheduler$ make
    ```
- Install module:
    ```shell
    raymond@ubuntu:~/mp2-rm-scheduler$ sudo insmod ./mp2.ko
    ```
- Test user program:
    ```shell
    raymond@ubuntu:~/mp2-rm-scheduler$ ./userapp
    usage: [period] [computation_time] [number of job]
    raymond@ubuntu:~/mp2-rm-scheduler$ ./userapp 10000 2000 3 & ./userapp 9999 2000 3 &
    [1] 2669
    [2] 2670
    raymond@ubuntu:~/mp2-rm-scheduler$ R, 2669, 10000, 2000go to sleep
    R, 2670, 9999, 2000go to sleep
    wake up -------------- 2670
    136.48 236.64
    138.29 238.37
    go to sleep ------------------ 2670
    wake up -------------- 2669
    138.29 238.37
    140.08 240.11
    go to sleep ------------------ 2669
    wake up ------------------ 2669
    146.48 252.90
    wake up ------------------ 2670
    146.49 252.91
    148.30 254.67
    go to sleep ------------------ 2670
    150.08 256.42
    go to sleep ------------------ 2669
    wake up ------------------ 2670
    156.48 269.21
    158.29 271.00
    go to sleep ------------------ 2670
    wake up ------------------ 2669
    158.29 271.00
    160.07 272.77
    go to sleep ------------------ 2669
    wake up ------------------ 2669
    166.48 285.58
    wake up ------------------ 2670
    166.49 285.61

    [1]-  Done                    ./userapp 10000 2000 3
    [2]+  Done                    ./userapp 9999 2000 3
    ```
- Remove module
    ```shell
    raymond@ubuntu:~/mp1-rm-scheduler$ sudo rmmod mp2
    ```
    
#### Code explanation:

- `admission_control()` function:

    - Calculate total workload of current schedule. To avoid floating point calculation, values are multiplied by 1000 to ensure correct integer calculation.
        ``` c
        list_for_each_safe(pos, q, &mp2_process_entries)
        {
            tmp = list_entry(pos, struct mp2_process_entry, ptrs);
            total_workload += (tmp->comp_time * 1000) / tmp->period;
        }
        ...
        total_workload += (comp_time * 1000) / period;
        if (total_workload > 693) return 1;
        else return 0;
        ```

- `yield_cpu()` function:

    - Set two conditions to prevent from setting the invalid timer
        ``` c
        if (jiffies < tmp->original_arrived_time + msecs_to_jiffies(tmp->period))
        {
            ...
            mod_timer(&tmp->wakeup_timer, tmp->original_arrived_time + msecs_to_jiffies(tmp->period));
            ...
        }
        else
        {
            ...
            mod_timer(&tmp->wakeup_timer, jiffies + msecs_to_jiffies(tmp->period));
            ...
        }
        ```
- `timer_calback()` function:

    - Implement two conditions for the timers set by `yield_cpu()` and the ones set by postpone action.
        ``` c
        if (process->timer_postpone == 1)
        {
            if (!wake_up_process(dispatching_thread))
            {
                mod_timer(&process->wakeup_timer, jiffies + msecs_to_jiffies(2));
            }
            else
            {
                process->timer_postpone = 0;
            }
        }
        else
        {
            process->state = TASK_RUNNING;
            if (!wake_up_process(dispatching_thread))
            {
                process->timer_postpone = 1;
                mod_timer(&process->wakeup_timer, jiffies + msecs_to_jiffies(2));
            }
        }
        ```
        `timer_postpone` is used when two timers arrive simultaneously. If two call_back functions are trigered too close, dispatch thread will only process once. To avoid this situation, if `wake_up_process()` returns 1, the another `timer_callback` will be trigered after several ms, instead of spinning this function call. This is because that `timer_callback` is in interrupt mode, spinning will cause starvation to all the other processes. Following figure illustrates this concept:
        ![](https://i.imgur.com/wpGzYix.png)

- `dispatching_func()` function:

    - This function is executed by a kernel thread. each time when the thread is waken will execute one iteration of while loop:
        ``` c
        set_current_state(TASK_INTERRUPTIBLE);
        schedule();
        while(!kthread_should_stop())
        {
            ...
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();
        }
        ```
    - Find the currently-runnable process which is in highest priority:
        ``` c
        shortest_period = ULONG_MAX;
        highest_task = NULL;
        ...
        list_for_each_safe(pos, q, &mp2_process_entries)
        {
            tmp = list_entry(pos, struct mp2_process_entry, ptrs);
            if (tmp->state == TASK_RUNNING)
            {
                if (tmp->period < shortest_period)
                {
                    shortest_period = tmp->period;
                    highest_task = tmp;
                }
            }
        }
        ```
    - Preempt the current running process and wake up the highest priority process. to make the highest process dominate a core and prevent from beinf preempted by system sckeduler, the process is set to FIFO mode and priority = 99:
        ``` c
        if (running_process != NULL && find_task_by_pid((unsigned int)running_process->pid) != NULL)
        {
            sparam_sleep.sched_priority = 0;
            sched_setscheduler(running_process->linux_task, SCHED_NORMAL, &sparam_sleep);
            set_task_state(running_process->linux_task, TASK_INTERRUPTIBLE);
            running_process = NULL;
        }
        if (highest_task != NULL)
        {
            wake_up_process(highest_task->linux_task);
            sparam_wake.sched_priority = 99;
            sched_setscheduler(highest_task->linux_task, SCHED_FIFO, &sparam_wake);
            running_process = highest_task;
        }
        ```

