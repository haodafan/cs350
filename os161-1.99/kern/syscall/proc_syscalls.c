#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>

#include <opt-A2.h>
#include <mips/trapframe.h>
#include <synch.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  
  // HAODA's WAITPID STUFF 
  p->terminated = true; // the process is NOW TERMINATED
  
  // Determine exit status 
  p->p_exitcode = exitcode;

/*// OLD DOCTRINE : DIRECT PROCTABLE 
  // We cannot yet proceed with full deletion... 
  lock_acquire(master_lock);
    // 3 conditions for deletion: 
    //     * no parent 
    //     * parent is terminated 
    //     * parent has called waitpid (parent will set safe_to_delete in waitpid)
    while (!p->safe_to_delete && p->p_parent != NULL && !p->p_parent->terminated)
    {
      cv_wait(master_condition, master_lock);
    }
  lock_release(master_lock);*/

  // NEW DOCTRINE: SKELETON PROCTABLE 
  // UPDATE THE PROCESS TABLiE 
  lock_acquire(master_lock); 
    for (unsigned int i = 0; i < array_num(proctable); i++)
    {
      struct skeleboi * skeleboi_in_question = array_get(proctable, i);

      // IF THE PROCESS IS THIS PROCESS AND THE PARENT IS NULL 
      //    THEN WE REMOVE THIS PROCESS FROM THE TABLE 
      if (skeleboi_in_question->p_id == p->p_id && skeleboi_in_question->p_parent == NULL)
      {
        array_remove(proctable, i);
        kfree(skeleboi_in_question);
      }
      // IF THE PROCESS IS THIS PROCESS AND THE PARENT IS NOT NULL
      //    THEN WE UPDATE THE PROCESS TO AN EMPTY SKELETON 
      else if (skeleboi_in_question->p_id == p->p_id)
      {
        skeleboi_in_question->p_this = NULL;
        skeleboi_in_question->terminated = true; 
        skeleboi_in_question->exitcode = exitcode;
      }
      // IF THE PROCESS'S PARENT IS NULL AND IT IS NOT THIS PROCESS 
      //    THEN THERE IS NO NEED TO REMOVE IT (since it will do it itself when it exits)
      //    AND WE NEED TO ENSURE THE NEXT IF STATEMENT DOESNT PERFORM A TLB MISS
      else if (skeleboi_in_question->p_parent == NULL)
      {
        // nothing happens
      }
      
      // IF THE PROCESS IS A CHILD OF THIS PROCESS AND THE PROCESS IS AN EMPTY SKELETON 
      //    THEN WE REMOVE THIS PROCESS FROM THE TABLE (since we can't call waitpid on it)
      else if (skeleboi_in_question->p_parent->p_id == p->p_id && skeleboi_in_question->p_this == NULL)
      {
        array_remove(proctable, i);
        kfree(skeleboi_in_question);
      }
    }
  lock_release(master_lock);

  // Wake up sleeping threads 
  cv_broadcast(master_condition, master_lock);

  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

#if OPT_A2 //CHANGE THIS BACK
// HAODA's FAT A2 IMPLEMENTATION 
//int
//sys_fork()
//{
	// Fork creates a new process (the child) that is a clone of the original (the parent)
	//    - after `fork`, both parent and child are executing copies of the same program
	//    - virtual memories of parent and child are identical at the time of the fork, 
	//      but may diverge afterwards
	//    - `fork` is called by the parent, but returns in both the parent and the child 
	//         - parent and child see different return values from fork. 
	
//}

int
sys_fork(struct trapframe * parent_tf, pid_t * retval)
{
	// creates a new process which is a copy of the parent process 
	//    -> new process with its own id number 
	//    -> return for the child process is 0, and the return for the parent is the child's pid 
	
	// How fork needs to be implemented: 
	//    1) we need to create a process structure 
	//    2) we need a new address space 
	//         -> We copy the parent's address space to the child's address space 
	//    3) it is now safe to assign the process id to the child and initiate the parent-child relationship 
	//    4) Now need to create a thread using thread_fork
	//         |-> Need to tell child thread where to execute from, so need to take
	//         |-> Take the saved trap frame of the parent process and putting it onto 
	//             the child's process thread's kernel stack 
	//    5) Set the retval to child's pid, and return 0 if everything is successful
	
	
	// 1) Create a process structure ()
	struct proc * child = proc_create_runprogram(curproc->p_name); 
	
	if (child == NULL) // if proc_create_runprogram fails, it's because there's not enough memory 
		return ENOMEM; // error code for no memory 
	
	
	// 2) Create new address space and copy its parent contents 
	//struct addrspace* child_addrspace = as_create();
	// int as_copy(struct addrspace *src, struct addrspace **ret);
	
	
	spinlock_acquire(&child->p_lock);
		int code = as_copy(curproc->p_addrspace, &child->p_addrspace); // no need to malloc 
	spinlock_release(&child->p_lock);
	
	if (code != 0)
		return ENOMEM; // error code for no memory
	// Set the child's address space (from curproc_setas)
	// child->p_addrspace = child_addrspace;
	
	// 3) Assign process id number (up to us) 
	//       Note: process does not have a pid field. We need to add the field ourselves 
	//             process ids cannot have a value of 0 or a negative value 
	//       haoda's proposed strategy: global pid incrementable number, lock every time u increment
	
	// The code to assign the process id number is located in proc/proc.c
	
	// 3.5) Create the parent-child relationship - simple solution: 
	// 	point the child to the parent process  
	// 	dynamic array of pointers to children
	child->p_parent = curproc;
  // Create relationship in proctable (NEW DOCTRINE : SKELETON PROCTABLE : STRATEGY 1)
	lock_acquire(master_lock); 
    for (unsigned int i = 0; i < array_num(proctable); i++)
    {
      struct skeleboi * skeleboi_in_question = array_get(proctable, i);
      if (skeleboi_in_question->p_id == child->p_id) // child process skeleton detected
      {
        skeleboi_in_question->p_parent = curproc;
        break;
      }
    }
  lock_release(master_lock);

	// 4) Create a thread 
	//      We want to add our new thread to our CHILD process => second param is child process
	//      thread_fork function param is the ENTRY POINT function => 
	//          |-> our new process is going to start execution in KERNEL mode
	//              thus, we need to pass in a kernel function enter_forked_process
	
	// First, we want to store the trap frame into the HEAP to prevent the case where 
	// the parent returns from fork before the child can execute. 
	struct trapframe* copy_tf;
	//spinlock_acquire(&curproc->p_lock);
		copy_tf = kmalloc(sizeof(struct trapframe)); // remember to free this!
		memcpy(copy_tf, parent_tf, sizeof(struct trapframe));
	//spinlock_release(&curproc->p_lock);
	
	code = thread_fork(curthread->t_name,
				child, 
				enter_forked_process, 
				(void*)copy_tf,
				0);
				
	// there was a problem with thread forks
	if (code != 0)
		return ENOMEM; // probably a memory error????? 
	
	// RETURN
	*retval = child->p_id; // if ur the parent
	return 0;
}

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
      userptr_t status,
      int options,
      pid_t *retval)
{
  (void) status;
  // wait PID is a BARRIER
  // it puts the parent process to SLEEP until the child process terminates
  // |-> if the child process is already terminated, the parent process will NOT sleep and simply return the return values (exit status, code) 
  //     from the child and continues to execute 

  // first you need to know who are your children
  // if the process id does not correspond to your children, then you must return an ERROR code
  bool isChild = false; 
  struct skeleboi * s_myChild;
  for (unsigned int i = 0; i < array_num(proctable); i++) // NEW DOCTRINE: SKELETON PROCTABLE (STRATEGY ONE)
  {
    struct skeleboi* skeleboi_in_question = array_get(proctable, i);
    if (pid == skeleboi_in_question->p_id)
    {
      // If this is NOT your child ... 
      if (skeleboi_in_question->p_parent == NULL || skeleboi_in_question->p_parent->p_id != curproc->p_id)
      {
        return ESRCH; // this is the code for 'no such process', idk if its the appropriate one
      }
      isChild = true; 
      s_myChild = skeleboi_in_question; 
      //if (s_myChild->p_this == NULL)
      //  return ERSCH; // no such process
      break;
    }
  }
  if (!isChild)
    return ESRCH; // no such process 




  // You need to determine if your child process has terminated or not 

  // If your child is still alive, you want to WAIT for your child to terminate - recommended: use a CV 
  // Since the parent waits for the child to terminate, the parent should call cv wait on the child's condition variable 
  lock_acquire(master_lock);
  while (!s_myChild->terminated)
  {
    cv_wait(master_condition, master_lock);
  }
  lock_release(master_lock);

  // Once you wake back up, your child process has terminated, thus you need to retrieve exit status and code 
  //
  // How are you going to save your exit status and exit code? 
  //   STRATEGY 1: create a skeleton structure 
  //   STRATEGY 2: Add a boolean to the process structure, and an exit status and exit code 


  // Note that exitstatus is your child's exit code
  // STRATEGY 2 : OBSELETE 
  //int exitstatus = _MKWAIT_EXIT(myChild->p_exitcode); //?????????????
  //myChild->safe_to_delete = 1; // we can put our dead child to rest :'( 
  //cv_broadcast(master_condition, master_lock);
  
  // STRATEGY 1
  int exitstatus = _MKWAIT_EXIT(s_myChild->exitcode); // ???
  // No need to tell dead child it can delete itself
  
  if (options != 0) {
    return(EINVAL);
  }

  // ??? 

  /* for now, just pretend the exitstatus is 0 */
  //exitstatus = 0;
  int result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
// END OF OLD CODE ============================================================
#else 

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}


#endif
