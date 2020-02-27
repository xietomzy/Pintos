Design Document for Project 1: User Programs
============================================

## Group Members

* Tom Xie <xie.tom.zy@berkeley.edu>
* Kevin Li <kevinlishirt@berkeley.edu>
* John Angeles <johnangeles@berkeley.edu>
* Coby Zhong <coby.zhong@berkeley.edu>

## Task 1: Argument Processing
### Data Structures and Functions:
No struct modifications are needed for this task.

A buffer `char *token` may be needed to hold the arguments as they are being parsed.
### Algorithm:
* In `start_process`,
  - get the first word using `strtok_r()` for the function
  - `load `to use as its argument `filename`
  - tokenize each argument
  - Use `strtok_r()` to tokenize file_name into separate words
* Push arguments onto stack
* Push enough empty space to align the stack
* Push a null pointer sentinel to ensure that `argv[argc]` is null
*  push addresses to words in right-to-left order such that `argv[0]` is at the lowest address of the stack
*  push address to `argv[0]` (`argv`) and then `argc`
*  finally push the fake return address

### Synchronization:
To avoid thread safety issues, we utilize `strtok_r` instead of `strtok`.

### Rationale:
This part is relatively simple to implement, so we plan to implement this task using the straight-forward algorithm we explained in Algorithms.


## Task 2: Process Control Syscalls
### Data Structures and Functions:
To implement `wait()`, we need some form of communication between the two that survives either exiting. We can achieve this by storing the following struct in the shared space between the parent and child thread:
```c
/*The following was heavily inspired by Discussion 2*/
struct child_status {
    struct semaphore load; /*Inform parent process when child has loaded*/
    bool successful_load; /*Whether the child loaded properly*/
    struct list_elem elem; /*We want the child statuses in a linked list for the parent*/
    struct lock ref_lock; /*Prevent ref_count from being concurrently modified*/
    int ref_cnt; /*0 = both parent and child dead | 1 = one alive, one dead | 2 = both alive*/
    tid_t childTid; /*Child thread ID*/
    int exit_code; /*Child exit code*/
    struct semaphore finished; /*0 = child running, 1 = child finished*/
}
```
This struct, along with the following functions and fields, will be added to `thread.h` to implement the aforementioned syscalls.
```c
struct child_status {...};
struct child_status *self_status; /*The wait_status of this process (this process as child)*/
struct list children_status; /*List of children as child_status's*/

/*Explained in Algorithms*/
void process_wait(tid_t child_tid) {...}
/*Explained in Algorithms*/
void process_exit(void) {...}
```

### Algorithms:
#### syscall.c
We will need to add cases to the kernel's `syscall.c` to handle the other syscalls. For `halt()`, the process is straightforward: shutdown the OS by calling `exit()` on the main thread. However, for the rest, we will need to safely acquire data from the user's stack for use in the kernel's stack.
To check for invalid pointers, we will use the following functions:
* `pagedir_get_page` --> check for unmapped addresses
* `is_user_vaddr` --> check is the address is in user virtual memory
* if the pointer itself is `NULL`, then terminate the process

#### `thread.h`
As detailed in Data Structures and Functions, we will need to add the `child_status` struct along with 2 fields and 2 functions to `thread.h`.  The child is free to die upon exit while the parent is tied up, since if the parent is still waiting for an exit code, it is available to them via the struct. Conversely, if the parent exits early, the child will still finish as expected.

The `child_status` struct itself will be created by the parent and passed to the child via pointer.

### Synchronization:
Since we are creating a struct to communicate between parent and child, we need to ensure that the second one to leave will free up the memory used by the struct. This is achieved by having a ref_count that has an accompanying lock to prevent concurrent modification bugs. Thus, if a child or parent sees that by decrementing ref_count, the count would reach zero, it frees the memory. This setup is insensitive to the order in which the child and parent die, since the ref_count ensures that whoever dies second cleans up the mess.

To block the parent until the child dies upon a `wait` syscall, a semaphore is present within the shared struct, and the child calls `up` once it completes, causing the parent's previous `down` to unblock.

To prevent executables from being modified ala lazy loading attacks, we will call `file_deny_write` upon load, and only call `file_close` (which implicitly calls `file_allow_write`) upon the executable finally exiting.


### Rationale:
Althought we thought about using an "ask-for-forgiveness" method with handling page faults due to its speed, we felt it would be too difficult to avoid introducing bugs whereby the kernel would not clean up all the resources that a user process had allocated.

## Task 3: File Operation Syscalls
#### Data Structures and Functions:
One of the structs we need to create in `syscall.h` is a file descriptor to map a file descriptor(fd) to a corresponding file every time a file is opened:
```
struct fileDescriptor
 {
 struct list_elem fileElem;
 int fd;
 char* fileName;
 File* fileptr;
 }
```
Each file descriptor will be "linked" to one file that (one-to-one relationship) and the file descriptor will used to conduct any reads or writes to the corresponding or linked file. We need a `list_elem` in the fileDescriptor struct since we need another struct that will allow for us to create a list of fileDescriptors.

```
struct list fileDescriptorList;
```
We will put the fileDescriptorList in `thread.h` because fileDescriptorList should not be a global variable since each thread should keep track of its own list of fileDescriptors


Another struct that we will need to utilize is a global lock that will manage the order of file operations.
```
struct lock globalLock;
```
Since the file operations are not thread safe, we utilized a global lock because locks can be passed/acquired from one thread to another easily.


#### Algorithms:
##### All the operations will be handled in `syscall.c`
For the open operation, we will utilize a `char* fileName` to perform the filesys_open function. We will then use necessary information to generate a fileDescriptor struct for the corresponding file.  For example if `char* fileName = 'guest.txt'`, we will do something like this to perform an open operation...
```
File* file = filesys_open(fileName);
FileDescriptor* fileD = malloc(sizeof(FileDescriptor));
fileD->fileptr = file;
fileD->fileName = fileName;

```
For every other operations, we will utilize the `int fd` field of the fileDescriptor struct to execute reads and writes to the corresponding files. These operations will begin by taking in the fileDescriptor's `int fd` value and iterate through the list of fileDescriptor to find the correct fileDescriptor. They will then use the necessary fields from the fileDescriptor to perform the corresponding filesys or file operation. For example, assuming that `int fd` is linked to the `group.txt` file and `getFileDescriptor(int fd)` function gets a  fileDescriptor pointer, we will perform read and write operations to the `group.txt` like this...
```
fileDescriptor* fileD = getFileDescriptor(fd);
file_write(fileD->fileptr, ...);
file_read(fileD->fileptr, ...);
```

#### Synchronization:
We will use the global lock to help synchronize any file operations. Since locks are held by a single thread and passed upon completion, a global lock will help ensure thread safety since the filesys and file operations are not thread-safe. When using a lock, we begin by first checking if the lock is being held by a thread and if it is not, we use the function `lock_init` to initialize the `globalLock` as a new lock. If the lock is being held by a thread, we use `lock_acquire` so that the lock is waiting for the operations before it to finish before acquiring the lock to allow it to begin the file operation. When that thread is completed with its operation, we use `lock_release` to release the lock so that the other operations waiting for the lock can begin their operation.

#### Rationale:
We decided to use a global lock instead of a semaphore since we felt that locks were better at achieving our purpose of thread safety since single operation is safer than a approach that used concurrency. In addition, we decided to put `struct list fileDescriptorList` in `thread.h` since we wanted each thread to have their own list of fileDescriptors versuses a global list of fileDescriptors.

## Additional Questions
### Invalid Stack Pointer

#### `sc-bad-sp.c`
On line 10 of the file `exec-bad-ptr.c`, the pointer being passed into the exec method has not been initialized thereby causing an invalid stack pointer.

### Stack Pointer Too Close to Page Boundary
#### `exec-bound.c`
In `get_bounary_area()`, variable`p` is set to the boundary of the page. The pointer then is subtracted by either `strlen(src) / 2` or `4096` thereby causing an overlap between the current page and the next page. Afterwards, the `strcpy` will then copy the data from `src` to the pointer location and causes the data to overlap between the two pages.

### Unchecked Project Requirement

We believe that the part of synchronization where only one thread is running at a time is not tested with the given test cases. In order to test successful thread blocking, we can test the value of the global lock, as the lock controls the status of the threads.
