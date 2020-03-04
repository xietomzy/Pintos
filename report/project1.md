Final Report for Project 1: User Programs
=========================================

## 1. The changes you made since your initial design document and why you made them (feel free to re-iterate what you discussed with your TA in the design review)

### 2.1 Task 1: Argument Passing

For task 1, we pretty much stuck with the algorithm and steps defined in the initial design document.  

However, we did add a few changes.

1. We had to initialize the interrupt frame and load executable using memset.  We did not mention this in the initial design doc.

2. We also pushed a null pointer for a return address, because it was mentioned in the spec, and we did not account for it in the initial desgin doc.

3. We had to add an if block to check whether or not the argument loadings were performed correctly, just to check for cases where we couldn't add the arguments to the stack.

4. We had to notify our current parent know that we are successful.  It's a new member of the thread struct that allows a parent to know whether or not the arguments have been successfully passed on to the stack.

### 2.2 Task 2: Process Control Syscalls

Task 2 remained relatively the same.  

We had a struct from the design doc to learn more things about children threads.

/*The following was heavily inspired by Discussion 2*/
struct child_status {
    struct list_elem elem; /*We want the child statuses in a linked list for the parent*/
    struct lock lock; /*Prevent ref_count from being concurrently modified*/
    int ref_cnt; /*0 = both parent and child dead | 1 = one alive, one dead | 2 = both alive*/
    tid_t childTid; /*Child thread ID*/
    int exit_code; /*Child exit code*/
    struct semaphore finished; /*0 = child running, 1 = child finished*/
}

























