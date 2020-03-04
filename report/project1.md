Final Report for Project 1: User Programs
=========================================

## 1. The changes you made since your initial design document and why you made them (feel free to re-iterate what you discussed with your TA in the design review)

For task 1, we pretty much stuck with the algorithm and steps defined in the initial design document.  

However, we did implement a few changes.

1. we used strtok_r within process_execute and passed that as an argument to thread_create to start a thread for start_process, rather than having it in start_process.  We did this because we wanted to make our code more organized in process_execute, take advantage of the abstraction of thread_create to execute a function, and prevent interfering with the skeleton code.

2. 
