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

Task 2 remained relatively the same, but some changes had to happen to our data structures.

#### A. The child_status struct

1. We had a struct from the design doc to learn more things about children threads, but we had to add more members. We had to add a semaphore called "load" that informs the parent process when the child has been loaded. This is important because in process_execute, when we call thread_create, we have to wait for the thread to actually create, because process_execute might return before thread_create finishes creating a thread.

2. We have a new boolean member called "successful load," used to indicate whether the child thread has been successfull loaded in process_execute.  This is important because we need to distinguish whether the child thread successfully creates or not.

3. A new lock member called "ref_lock", which is a lock that must be acquired in order to modify the ref_count of a thread.  There may exist race conditions when a thread's ref_count needs to be changed.

Our thread struct remains virtually the same as what we have said in the design doc.

#### B. Algorithms

##### syscall.c

Halt remained virtually the same.

1. To check for invalid pointers, we created a new, simple boolean function that handles all of the logic.  It's called validate.  Unlike in our design doc, this function checks for unmapped addresses, whether or not we are accessing valid, user memory, and for NULL pointers.  It's just to help make our code even cleaner.

Not necessarily something we noted in the design doc, but we did write lots of our code in process_wait and process_execute.  The reason being was that in discussion 2, a lot of the logic was written in process_wait and process_exit rather than making redundant code in syscall_handler, and we heeded that for cleaner code.

### 2.3 Task 3: File Operation Syscalls

We had a struct named fileDescriptor that corresponds to a file any time a file is opened.

1. We just removed the fileName member because it was unnecessary, and all we really needed was the file descriptor number, and a pointer to the file.

#### A. Algorithms

The algorithm remains virtually the same for open. We also still use a global lock for handling file opens as they are vulnerable to race conditions.

## 2. A reflection on the project – what exactly did each member do? What went well, and what could be improved?

Each group member originally split up doing different tasks.  We had one group member do the first part, two members attempt the second part (which eventually all of us in the end started to work on due to numerous bugs) and our last group member attempt the third part.

This idea was good in a sense that a certain member doing a certain task completely understands what they had to do for their part.  However, this resulted in a much harder time for other group members to help contribute when a person focusing on a specific part needed help on a certain task.  It was tough to debug because the group member would have to explain to others what their thought process was.

Furthermore, we did not start the project until after the checkpoint due to various reasons.  However, those reasons shouldn't have stopped us from getting started on this project ahead of time.  This led to a time crunch for our group which is never really a good thing, and we should have spread out our work and planned ahead for inconveniences.

Of course, we could have improved the time at which we start the project to let algorithms and ideas come up in our minds to prevent time crunches.  We also think that working together on a certain part together would benefit us all.  It would let us all know the context of our code to enhance debugging and prevent merge issues.

























