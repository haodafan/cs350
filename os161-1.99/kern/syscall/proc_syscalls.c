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
#include <kern/fcntl.h>
#include <vfs.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  //(void)exitcode;

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
				
  // We no longer need the kernel's copy of the trapframe 
  //kfree(copy_tf);

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
  *retval = curproc->p_id;
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

  // But we do need to delete the dead child's skeleton! 
  //kfree(s_myChild); // maybe we dont
  
  if (options != 0) {
    return(EINVAL);
  }


  /* for now, just pretend the exitstatus is 0 */
  //exitstatus = 0;
  int result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}


// sys_execv(const char *program, char ** args)
int
sys_execv(userptr_t program, userptr_t args)
{
  char ** charargs = (char**) args; // temporary conversion to make things easier to debug
  
  // Step 1: Count the number of arguments and copy them into the kernel 
  int argnum = 0; 
  while (charargs[argnum] != NULL) // dis right??? ?_? 
  {
    argnum++; 
  }

  ///kprintf("argnum: %d\n", argnum); // DEBUGGING
  
  char ** kargs = kmalloc(sizeof(char*) * argnum); 
  for (int i = 0; i < argnum; i++)
  {
    unsigned int got = 0; 
    char * kargv = kmalloc(sizeof(char) * (strlen(charargs[i]) + 1));
    
	int res = copyinstr((const_userptr_t) (charargs[i]), kargv, strlen(charargs[i]) + 1, &got);
    if (res != 0) return res; // lol
    //kprintf(kargv);
    //kprintf("\n"); // DEBUGGING   
	kargs[i] = kargv; 
  }

  // Step 2: Copy program file into kernel 
  // The program path passed in is a pointer to string in user-level address space. 
  // Execv purges the old address space and replaces it with a new one, thus to prevent that problem we will need 
  // to copy the string into the kernel space before destroying the user space. 

  int program_charsize = 0; 
  while (((char*)program)[program_charsize] != '\0')
  {
    program_charsize++; 
  }
  
  // Does this count as passing to kernel stack ??? :'( 
  char * progname = kmalloc(sizeof(char) * (program_charsize + 1)); // +1 for null terminator
  for (int i = 0; i < program_charsize + 1; i++) // idk if i should have used strcpy() instead 
  {
    progname[i] = ((char*)program)[i];
  }

  // EXTRA STEP: SAVE THE CURRENT ADDRSPACE SO WE CAN DELETE IT? 
  struct addrspace *oldas = curproc_getas(); 
  
  // Step 3,4,5 : done using runprogram 
  // ========== Here lies the copy-pasted code of runprogram ==========
  // progname is the parameter from runprogram 
  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;

  /* Open the file. */
  result = vfs_open(progname, O_RDONLY, 0, &v);
  if (result) {
    return result;
  }

  /* We should be a new process. */
  // KASSERT(curproc_getas() == NULL); // Haoda change: not necessarily!

  /* Create a new address space. */
  as = as_create();
  if (as ==NULL) {
    vfs_close(v);
    return ENOMEM;
  }

  /* Switch to it and activate it. */
  curproc_setas(as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    vfs_close(v);
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    return result;
  }

  // Haoda change: We do enter_new_process and end code things later

  // ========== END OF COPY PASTED RUNPROGRAM ==========

  // Step 6: Need to copy arguments into new address space. 
  vaddr_t ogstackptr = stackptr; 

  // The user stack is composed of 2 parts: 
  // (bottom of stack)  -->  (top of stack) 
  // 1) THE STRING ARGS ; 2) THE POINTERS WHICH POINT TO THE STRING ARGS 
  
  userptr_t * userargsptr = kmalloc(sizeof(char*) * argnum);  
  //panic("this part of the code is reached 0");
  // 6.1 : Add sring args onto the user stack (needs to be on the STACK, not the heap)
  int allocated_size; 
  for (int i = 0; i < argnum; i++) 
  {
	  // We need to ensure we have enough space for the string 
	  unsigned int length = strlen(kargs[i]) + 1; // +1 for null terminator
	  unsigned int used = 0; 
	  // We know each char is 1 byte 
	  allocated_size = ROUNDUP(length, 8); 
	  stackptr = stackptr - allocated_size; 
	  //panic((char*)kargs[i]); 
	  result = copyoutstr(kargs[i], (userptr_t) stackptr, length, &used); // Copy into stack location
	  if (result != 0) return result; // idk this seems right i sppose 
	 //kprintf(((char*)stackptr)); // debugging
         //panic(((char*)stackptr)); 
	  userargsptr[i] = (userptr_t) stackptr; // convert to user pointer 
	 //panic("the end of the step 6.1 for loop");
	 //kprintf((char*)stackptr);
	 //kprintf("\n"); // DEBUGGING 
  }
  
  
  // 6.2 : Add string pointers onto the user stack 
  //       Note that stack items are 8-byte aligned, while pointers (like vaddr_t) are 4 byte
  vaddr_t argvptr; 

  // NULL will be on the BOTTOM of the stack, and thus is added first 
  stackptr = stackptr - 4; 
  char** nullptr = (char**) stackptr; 
  *nullptr = NULL; 
  
  // Push in backwards order ... 
  for (int i = argnum - 1; i >= 0; i--)
  {
	  stackptr = stackptr - 4; // 4 byte alignment ?? 
	  //result = copyout(userargsptr[i], (userptr_t) stackptr, sizeof(char*)); // I think this is how you use copyout
	  userptr_t * thisptr = (userptr_t *) stackptr; 
	  *thisptr = userargsptr[i];
	  if (i == 0) argvptr = stackptr; // set argument pointer to first argument
	  //kprintf((char*)userargsptr[i]);
	  //kprintf("\n"); //DEBUGGING
  }
  
  stackptr = stackptr - 4; // artificial padding

  // 6.1 : Copy Arguments onto the user stack as part of as_define_stack
  //*stackptr = argnum; // argc 
  //stackptr += 4; 
  //for (int i = 0; i < argnum; i++)
  //{
    //int got; 
    //char * argv = malloc(sizeof(char) * (strlen(args[i]) + 1)); 
    //result = copyoutstr(kargs[i], argv, strlen(args[i]), &got);
    
  //  if (result != 0)
  //    return result; 

  //  *stackptr = argv; 
  //  stackptr += 4; 
  //}
  //panic("this part of the code is reached 2"); 
  // Step 7: Delete old addrspace 
  kfree(oldas); 

  // Step 8: Call enter_new_process with address to the arguments on the stack, 
  //         the stack pointer, and the program entry point 
  
  /* Warp to user mode. */
  //kprintf(*((char**)argvptr));
  enter_new_process(argnum/*argc*/, (userptr_t) argvptr /*userspace addr of argv*/,
        stackptr, entrypoint); // Note: we will do argument passing later
  (void) ogstackptr; // idk if we use it
  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
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
