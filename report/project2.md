Final Report for Project 2: Threads
===================================

Replace this text with your final report.

## Task 1: Efficient Alarm Clock

### Changes made to original design doc:

- In `thread.h`, we changed the member of our thread struct called `sleepTicks` to another name called `wakeup_mark`. Originally, `sleepTicks` denoted how many seconds until the thread could be put to the ready queue, but instead we added `wakeup_mark` which denotes the time ever since the OS started at which the thread should wake up. This would make our function more efficient since all we would need to do to check if a thread needs to wake up is to do a simple comparison with `timer_ticks()`.
- We created a new function called `wakeup()` that instead of `sleep_handler()` puts threads to the ready state using `thread_unblock()`. We originally wanted to call `wakeup()` in `thread_schedule_tail()`, but this wouldn't work out because if a thread somehow never called `schedule()`, then we would never have the other threads that called `timer_sleep()` ever wake up, so we instead put `wakeup()` in `thread_tick()`.
- Furthermore, we created a new static field in `timer.c` called `next_wakeup` that denotes the time at which we have to wakeup. This is needed because it allows us to call `wakeup()` only when we have a thread that needs to be woken up. In `thread_tick`, we only call `wakeup()` if the current tick matches next_wakeup.

## Task 2: Priority Scheudler

### Changes made to original design doc:

- Our algorithms for `lock_aquire()` and `lock_release()` remained practically the same. However, we've had synchronization isues when attempting to access the lock's semaphore's waiting list, so we had to create a new member of the semaphore struct called `lock_acq_list`, which is a list of threads that are trying to acquire a certain lock.

## Reflection on the project

- Just like the previous project, we decided to split up the work, with one person implementing part 1 and the rest of us implementing part 2. The rest of us overlooked on the person implementing part 1, but eventually we all reconvened to work on part 2. We would have focused on the same part together instead of initially splitting up the work, but it was slightly tough to do considering the current situation with online classes.
- Debugging was a bit more comprehendable for us compared to doing so in project one because we all focused our energy towards task 2 since task 1 was relatively easier. Debugging was still tricky because of how subtle synchronization bugs can arise like that mentioned in task 2. What also helped was more effort on the design document. Since we put more effort into the design document, we spent less time writing a base implementation and more thinking about debugging techniques.
- Another good thing we did was starting earlier. We forced ourselves to start ahead of time which helped reduce a lot of stress in the long-run despite commitments to other classes.
- For next time, we could improve on making an even more solid design document to ensure ample time for debugging, and everyone working on the same task together.
