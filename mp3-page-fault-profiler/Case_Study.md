# MP3 Analysis (Case Study)

### Case Study 1: Thrasing and locality ###

Following two figures illustrate the statistics of two scripts respectively:

``` bash
    nice ./work 1024 R 50000 & nice ./work 1024 R 10000 &
```
<img src="https://i.imgur.com/4CHRkgb.png" width="250">
``` bash
    nice ./work 1024 R 50000 & nice ./work 1024 L 10000 &
```
<img src="https://i.imgur.com/QhzoJsB.png" width="250">

Analysis:

- The difference between two scripts are the memory access policies of the second execution, which are random-based and locality-based. When a process has the locality-based memory access, the cached data have higher possibility to be reused. Thus, the minor page fault can be much smaller than the process running with random-based policy. 
- The concept is same as utilizaton. because that `utilization = stime + utime`, as page faults increase, the utilization of each iteration in work process also increase.
- And Why are the graphs of random-based processes curved? This is because that the former accesses of pages are usually "not cached" and which spend times to wait for system's initialization and assignment; latter accesses are usually cached, so less page faults can be caused.
- Because that the utilization of memory does not exceed the physical memory space, no swaps are conducted and thus no major page fault.

### Case Study 2: Multi Programming ###

Following figures illustrates the different extent of multi-programming that all of them execute with random-based, 200MB access policy:

1 process executes at a time:

<img src="https://i.imgur.com/9MzpRYi.png" width="250">

5 processes execute at a time:

<img src="https://i.imgur.com/w4h22KF.png" width="250">

11 processes execute at a time:

<img src="https://i.imgur.com/vDIa0Ez.png" width="250">

These executions does not show the significant difference. So, I tried the more aggressive memory utilization: random-based, 512MB access policy:

1 process executes at a time:

<img src="https://i.imgur.com/Y5bg3mY.png" width="250">

5 processes execute at a time:

<img src="https://i.imgur.com/ZEPjWy1.png" width="250">

10 processes execute at a time:

<img src="https://i.imgur.com/qhp2RYx.png" width="250">

Note that executes 11 processes execute in the same time will trigger OOM to kill the process since the total memory usage is exceed the pysical memory too much (5.5GB/4GB).

Interestingly, the major fault counts starts to tremendously increase at the later interations, and the utilization of processes are also drastically increased. This is certainly because that the system started to swap for giving more virtual memory to process that is more than pysical memory. Note that the minor page fault does not have significant change since the major fault start because that the major faults are usually triggered when system tries to deal with minor faults but the page does not exist in the DRAM.
