Design Document for Project 2: User Programs
============================================

## Group Members

* Tom Xie <xie.tom.zy@berkeley.edu>
* Kevin Li <kevinlishirt@berkeley.edu>
* John Angeles <johnangeles@berkeley.edu>
* Coby Zhong <coby.zhong@berkeley.edu>

## Task 1: Efficient Alarm Clock
### Data Structures and Functions:
To begin, we'll need a way to keep track of how many ticks are left to sleep in the thread struct. This will be as simple as adding
```clike
int sleepTicks;
```
to `struct thread` in `thread.h`. Then, in `thread.c`, we will add the following helper function
```clike
void *sleep_handler(struct thread *thread, void *aux);
```
which is explained in Algorithms.

### Algorithms:
### timer.c
In `timer_sleep`, we will first validate that `ticks` is indeed greater than zero, returning if this is not the case. Then, instead of spinning in a circle in `timer_sleep`, we will instead set the thread's `sleepTicks` to the passed in value. Finally, we will call `thread_block` to put ourselves to sleep. The reason we do not need to acquire a lock to modify `sleepTicks` is explained in **Synchronization**. 

### thread.c
`sleep_handler` will do the following for each thread:
- If the thread's `sleepTicks` is greater than zero, decrement `sleepTicks` by one
- Else, if the thread's `sleepTicks` is zero and the thread's status is `THREAD_BLOCKED`, we will call `thread_unblock` on it.

`sleep_handler` will be called on all threads in `thread_schedule_tail` using `thread_foreach`.

### Synchronization:
It may be surprising that we chose not to use a lock or any other synchronizations primitives for protection against concurrency issues for `sleepTicks`. The rationale is explained below.

### Rationale: 
We found it unnecessary to include a lock (or any other synchronization construct) for the following reasons:
- The lock would need to be acquired every tick, adding overhead to thread management
- Mistakes in having the lock being held by sleeping threads while interrupts are disabled would result in problems
- Most importantly, since only one thread can decrement the value, and it is not interruptible when it happens, with the only other modification occurring at the initial set, it is not possible for concurrency bugs to occur.

## Task 2: Priority Scheduler
### Data Structures and Functions:
#### thread.c/thread.h
To begin, we must find a way to sort the `ready_list` by priority. One way to accomplish this is to use a comparator that would compare the priorities of two threads.
```c
list_less_func compare_priority(thread a , thread b) 
```
We would pass this comparator into the list functions, such as `list_sort` and `list_insert_ordered`. However, since these list functions are not thread-safe, we need to use a lock for every time we modify the list. 
```c
struct lock ready_list_lock;
```
In addition to a comparator, each thread must keep track of certain variables that may change throughout a process. One thing that may change is the priority of the thread. Since higher priority threads are allowed to donate to lower priority threads, we need to keep track of the original priority of each thread. As a result, we will add a field to the `struct thread` in `thread.h` that keeps track of the original priority.
```c
struct thread {
    /* Other fields of thread */
    int og_priority;
}
```
We will also need to keep track of the locks that a thread may be holding or waiting on since this will help us determine if priority donation is necessary to complete a higher priority task. As a result, we will be adding two fields to the `struct thread` in `thread.h` that will keep track of the lock the thread is waiting for and the locks that the thread is holding.
```c
struct thread {
    /* Other fields of thread */
    struct lock *waiting_lock; 
    struct list held_locks;

}
```
***Note that we do not need to make waiting locks a list of locks since a thread can only be waiting for one lock at a time whereas a thread can be holding multiple locks at one time.***
Finally, since we are modifying the order of `ready_list`, we must  modify the function`thread_unblock` so that `thread_unblock` inserts threads into the `ready_list` by order of priorities. 
#### synch.c
In addition to changes in `thread.c` and `thread.h`, we need to make some changes to `synch.c` to handle priority donation. 
- Currently, `lock_acquire` does not perform priority donation, but in order to prevent priority inversion, we must first check if another thread is holding the lock being acquired. In the case that another thread is holding the lock, we must have the current thread donate priority to the thread with the lock. 
- Likewise, `lock_release` currently does not takes into consideration the effective priority of a thread when determining which thread will hold the lock after the lock been released, so we must implement this into the function `lock_release`. 
- Finally, we must alter the way semaphores and conditional variables are constructed. In order to account for the changes to priority scheduling and priority donation, both semaphores and conditional variables must maintain a sorted list `waiter` whenever we call `sema_up()`. In addition, because `waiter` is in `synch.c`, we cannot use locks when dealing with list operations and must instead use interrupts.

### Algorithm:
#### Choosing the next thread to run
This simply requires sorting the ready_list by priority (using list_sort with the compare_priority function defined above) and popping the list. 

```c 
static struct thread *
next_thread_to_run (void) {
    if (list_empty (&ready_list))
        return idle_thread;
    else
        list_sort(&ready_list, compare_priority);
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
}
```
#### Priority Scheduling for Semaphores and Locks
Since locks are just binary semaphores, we only need to change the functions for semaphores, specifically `sema_up`. We just need to change which thread is popped off and unblocked by sorting the `sema->waiters` list using `list_sort` with the `list_less_func compare_priority`. 
#### Priority Scheduling for Conditional Variables
Conditional variables operate similar to semaphores, so we just need to change `cond_signal` such that we signal the thread with the highest priority to wake up, which involves sorting `cond->waiters` using `list_sort` with the `list_less_func compare_priority`.
#### Changing a thread’s priority
To change a thread's priority, we need to call `thread_set_priority`, and if the thread's priority is lower than the highest priority thread on `ready_list`, then we should immediately yield (as mentioned in the spec).

#### Acquiring a Lock/Priority Donation/Effective Priorities
To acquire a lock, we must first see if the lock has a holder. Then, if the current thread's priority is less than the lock holder's priority, we 
* add the current thread to the lock's waiting list
* set the `waiting_lock` of current thread to the current `lock` 

If the current thread's priority is greater than the lock holder's priority (condition for priority donation) we
* add the current thread to the lock's waiting list
* set the `waiting_lock` of current thread to the current `lock`  
* execute priority donation in a while loop (to ensure nested priority donation)
    * check if the current thread's priority is the greatest among the lock holder's priority and the lock's waiters
        * if true then donate the priority (need to sync this because interrupts can be altered by the scheduler)
    * while the lock_holder has a lock it's waiting on, check if priority donation is needed for the lock_holder thread


We can now call `sema_down` to actually acquire the lock, and after that completes we know that the lock holder should now be the current thread. We must also remove the current thread from the waiting list of the lock. 
```c
void lock_acquire(struct lock *lock) {
    
    struct thread *curr_thread = thread_current();

    if (curr_thread->priority < lock->holder->priority) {
        list_push_front(lock->semaphore->waiters, curr_thread->elem);
        curr_thread->waiting_lock = lock;
    } else if (curr_thread->priority > lock->holder->priority) {
    
        list_push_front(lock->semaphore->waiters, curr_thread->elem);
        curr_thread->waiting_lock = lock;
        
        while(lock) {
            if (curr_thread.priority > 
            list_max(lock->semaphore->waiters, compare_priority) 
            && curr_thread.priority > lock->holder.priority) {
                intr_disable(); // interrupts may change the priorities
                lock->holder->priority = curr_thread->priority
                intr_enable();
                curr_thread = lock->holder; // check 
                lock = curr_thread->waiting_lock
            } else
                 break;
        }
    }
    sema_down(&lock->semaphore);
    lock->holder = curr_thread;
    remove curr_thread from lock's waiting list
} 
```
#### Releasing a Lock
When releasing a lock, we want to set the current thread's priority to either its original priority if it is not holding any other locks or the maximum priority of the waiting lists of the other locks it holds, because the lock holder could still be needing a priority donation for the other locks it holds. We then change the lock holder to be the thread with the highest priority in the lock's waiting list and put it on `ready_list` so that the scheduler can run it again. We finally call `sema_up` to actually release the lock and check if our thread does not have the highest priority; if it does, then we must immediately yield the CPU to higher priority threads.
```c 
void lock_release(struct lock *lock) {
    intr_disable() // needed to sync waiting lists
    
    set current thread's priority to the maximum priority of 
        the threads in each waiting list of 
        held_locks if that max priority is greater than the current thread's 
    else 
        set current thread's priority to og_priority
        
    lock->holder = thread with highest priority from lock’s waiting list
    intr_enable()

    put lock->holder on ready list in scheduler

    sema_up(&lock->semaphore);
    if (curr_thread does not have highest priority)
        thread_yield();
}
```


### Synchronization:
There are multiple race conditions that we must consider when implementing priority scheduling. 
1. One potential race condition that we must address is the modification of lists. Because list operations are not thread-safe, it means that if multiple threads are trying to modify a list at once, we must use either a lock or disable/enable interrupts to prevent multiple threads from modifying a list at one time.
    * `lock_acquire` or `intr_disable` before the list is modified.
    * List Modification Operation (such as `list_insert_ordered`)
    * `lock_release` or `intr_enable` when the list is finished being modified
2. Another potential race condition that we must address is the modification of priorities. Since priorities might get altered by interrupts from the scheduler, we need to disable interrupts before we perform priority donation. Afterwards, we complete the priority donation, we re-enable interrupts. The disabling of interrupts before priority donation is crucial since thread priorities are recalculated every 4th clock tick, and if we do not disable interrupts, the priority donation could become messed up when the scheduler interrupts the priority donation.    

### Rationale: 

#### Rationale for algorithms

* For `lock_acquire` our group feels pretty confident about the way we've implemented it.  It's somewhat not too hard to conceptualize because the code that we've written for lock_acquire is somewhat based off of how a scheduler would donate priority based off the discussion worksheet; when a thread is waiting for a lock, we iteratively donate our priority until some other thread can run, and then put the thread to sleep.  There shouldn't be any busy waiting since once we are done with iteratively donating our priorities, we will call sema_down which will cause the thread to sleep until the lock is available.  
    - Our runtime should be linear in the size of the waiters list of the lock.
* For `lock_release`, we may encounter issues upon implementation as our pseudocode is not as concrete as that of lock_acquire.  The length of the pseudo-code should roughly match our actual implementation.  In terms of correctness, we've followed how a thread would generally release a lock based off of the discussion worksheet, where we set the current thread's priority depending on whether or not there are other held locks the current thread has.

## Additional Questions

1. In class, we studied the three most important attributes of a thread that the operating system stores when the thread is not running: program counter, stack pointer, and registers. Where/how are each of these three attributes stored in Pintos? You may find it useful to closely read `switch.S` and the schedule function in thread.c. You may also find it useful to review Lecture 43 on September 10, 2019.


    A. For the stack pointer, switch_threads() in thread.c according to the spec is an assembly language routine that saves both the registers on the stack, and the CPU’s current stack pointer.  The procedure is as follows:
    - Save the caller's register state
    - Save the current stack pointer 
    - Restore stack pointer from new thread's stack
    - Restore caller's register state


2. When a kernel thread in Pintos calls thread_exit, when/where is the page containing its stack and TCB (i.e., struct thread) freed? Why can’t we just free this memory by calling palloc_free_page inside the thread_exit function?

    A. After we disable interrupts and remove the thread element from the list, we call schedule to schedule a new thread, which afterwards calls thread_schedule_tail that destroys the struct thread.  As noted from the comments in thread_schedule_tail, we can't free this memory because the memory was not obtained via palloc().

3. When the thread_tick function is called by the timer interrupt handler, in which stack does it execute?

    A. Because all interrupts are handled in kernel mode, it executes in the kernel stack.

4. Test case for existence of flawed priority donation:

    Q. Suppose we had Threads 1, 2, 3 and 4 with priorities 10, 20, 30 and 40 respectively.  Threads 2 and 3 are waiting for thread 1 to complete because it has acquired a lock that both of them need. Thread 4, on the other hand, is also waiting for thread 2 to complete because thread 2 has a lock that it wants to retrieve.

```
             L
         <-- Thread 2  <-- Thread 4  
Thread 1     20            40
10
L        <-- Thread 3
             30   
                 

```

* For the expected outcome, thread 2 should finish first because thread 4 has donated its priority to thread 2 in order to retrieve the lock. 
* With the bug, thread 3 will run finish because it has a higher base priority.
* Thus, we can have a test case where the expected will be where thread 2 prints first, followed by thread 3. All we need to do is insert print statements at the end of threads 2 and 3.

```C
void print2() {
    printf("%s\n", "Thread 2 complete!");
}

void print3() {
    printf("%s\n", "Thread 3 complete!");
}
```

* This means that our expected output should be:

```
Expected:
Thread 2 complete!
Thread 3 complete!
```

* If we were to fail, then our output would have been:

```
Thread 3 complete!
Thread 2 complete!
```
