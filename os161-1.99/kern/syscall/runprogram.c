/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>

#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
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
	KASSERT(curproc_getas() == NULL);

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

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

int runprogram_args(char* progname, char** args, int nargs)
{
  // CODE COPIED FROM EXECV 


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
  
  userptr_t * userargsptr = kmalloc(sizeof(char*) * nargs);  
  //panic("this part of the code is reached 0");
  // 6.1 : Add sring args onto the user stack (needs to be on the STACK, not the heap)
  int allocated_size; 
  for (int i = 0; i < nargs; i++) 
  {
	  // We need to ensure we have enough space for the string 
	  unsigned int length = strlen(args[i]) + 1; // +1 for null terminator
	  unsigned int used = 0; 
	  // We know each char is 1 byte 
	  allocated_size = ROUNDUP(length, 8); 
	  stackptr = stackptr - allocated_size; 
	  //panic((char*)kargs[i]); 
	  result = copyoutstr(args[i], (userptr_t) stackptr, length, &used); // Copy into stack location
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
  for (int i = nargs - 1; i >= 0; i--)
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
  //kfree(oldas); 

  // Step 8: Call enter_new_process with address to the arguments on the stack, 
  //         the stack pointer, and the program entry point 
  
  /* Warp to user mode. */
  //kprintf(*((char**)argvptr));
  enter_new_process(nargs/*argc*/, (userptr_t) argvptr /*userspace addr of argv*/,
        stackptr, entrypoint); // Note: we will do argument passing later
  (void) ogstackptr; // idk if we use it
  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
}

