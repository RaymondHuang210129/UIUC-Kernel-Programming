# CS423 Spring 2020 MP2
### Rate-Monotonic CPU scheduling module

#### Run program:

- Compile module:
    ```shell
    raymond@ubuntu:~/mp3-fault-profiler$ make
    ```
- Install module:
    ```shell
    raymond@ubuntu:~/mp3-fault-profiler$ sudo insmod ./mp3.ko
    ```
- Test user program:
    ```shell
    raymond@ubuntu:~/mp2-rm-scheduler$ cat /proc/devices | grep node
    247 node
    raymond@ubuntu:~/mp2-rm-scheduler$ mknod node c 247 0
    raymond@ubuntu:~/mp2-rm-scheduler$ nice ./work 1024 R 50000 & nice ./work 1024 R 10000 &
    [1] 7411
    [2] 7412
    raymond@ubuntu:~/mp2-rm-scheduler$ A work prcess starts (configuration: 1024 0 50000)
    A work prcess starts (configuration: 1024 0 10000)
    [7411] 0 iteration
    [7412] 0 iteration
    [7412] 1 iteration
    [7411] 1 iteration
    [7412] 2 iteration
    [7411] 2 iteration
    [7412] 3 iteration
    [7411] 3 iteration
    [7412] 4 iteration
    [7411] 4 iteration
    [7412] 5 iteration
    [7411] 5 iteration
    [7412] 6 iteration
    [7411] 6 iteration
    [7412] 7 iteration
    [7411] 7 iteration
    [7412] 8 iteration
    [7411] 8 iteration
    [7412] 9 iteration
    [7411] 9 iteration
    [7412] 10 iteration
    [7411] 10 iteration
    [7412] 11 iteration
    [7411] 11 iteration
    [7412] 12 iteration
    [7411] 12 iteration
    [7412] 13 iteration
    [7411] 13 iteration
    [7412] 14 iteration
    [7411] 14 iteration
    [7412] 15 iteration
    [7411] 15 iteration
    [7412] 16 iteration
    [7411] 16 iteration
    [7412] 17 iteration
    [7411] 17 iteration
    [7412] 18 iteration
    [7411] 18 iteration
    [7412] 19 iteration
    [7411] 19 iteration

    [1]-  Done                    nice ./work 1024 R 50000
    [2]+  Done                    nice ./work 1024 R 10000
    raymond@ubuntu:~/mp2-rm-scheduler$ sudo ./monitor > output.txt
    ```
- Remove module
    ```shell
    raymond@ubuntu:~/mp1-rm-scheduler$ sudo rmmod mp3
    ```
    
#### Code explanation:

- `profiler_buffer`:

    - a special memory space instantiate in kernel but is mapped to userspace and user program can access without kernel's assist

        - in `init()` function: allocate a large space with `vmalloc` which can be discontinuous in physical memory
            ``` c
            ...
            profiler_buffer = vmalloc(PAGE_SIZE * 128);
            for (i = 0; i < 128 * PAGE_SIZE; i += PAGE_SIZE)
            {
                SetPageReserved(vmalloc_to_page(profiler_buffer + i));
            }
            ...
            ```
        - in `registration()`: initial `profiler_buffer_offset` for later memory access
            ``` c
            ...
            if (mp3_timer == NULL)
            {
                ... // setup timer
                profiler_buffer_offset = profiler_buffer;
            }
            ...
            ```
        - in `mp3_work_function()`: y timer for each 50ms and grab stime, utime, and utilization from kernel PCB, and write to `profiler_buffer`
            ``` c
            list_for_each_safe(pos, q, &mp3_process_entries)
            {
                tmp = list_entry(pos, struct mp3_process_entry, ptrs);
                if (get_cpu_use((int)tmp->pid, &statistic.minor_fault_count, &statistic.major_fault_count, &utime, &stime))
                { ... }
                else
                {
                    statistic.current_jiffies = jiffies;
                    statistic.utilization = (utime + stime) * 100 / (jiffies - tmp->previous_jiffies);
                    if (profiler_buffer_offset != NULL && profiler_buffer_offset != profiler_buffer + 128 * PAGE_SIZE)
                    {
                        *(struct mp3_statistic *)profiler_buffer_offset = statistic;
                        profiler_buffer_offset += sizeof(struct mp3_statistic);
                    }
                    ...
                }
            }
            ...            
            ```
        - in `mp3_cdev_mmap()`: triggered when a process in user space calls `mmap`, the allocated memory in kernel space is mapped to userspace page by page
            ``` c
            unsigned long pfn, i;
            for (i = 0; i < 128 * PAGE_SIZE; i += PAGE_SIZE)
            {
                pfn = vmalloc_to_pfn(profiler_buffer + i);
                if (remap_pfn_range(vma, (vma->vm_start + i), pfn, PAGE_SIZE, vma->vm_page_prot))
                {
                    printk(KERN_ALERT "error remapping %ld", i);
                }
            }
            ``` 