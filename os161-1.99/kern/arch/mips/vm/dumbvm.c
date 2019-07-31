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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

#include <syscall.h>
#include <kern/wait.h>
#include <mips/trapframe.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static volatile struct pageitem * coremap; 
static volatile unsigned long totalpages; 
static volatile bool memory_for_bootstrap = true;

static paddr_t paddrlow; 
static paddr_t paddrhigh; 

struct pageitem
{
	//paddr_t paddr; 
	bool occupied; 
	unsigned int blocksize; // The number of contiguous pages after it with the same 'occupation status'
};

void
vm_bootstrap(void)
{
	ram_getsize(&paddrlow, &paddrhigh);
	totalpages = (paddrhigh - paddrlow) / PAGE_SIZE; 

	// Calculate coremap size 
	int coremapsize = sizeof(struct pageitem) * totalpages;
	int coremappages = (coremapsize / PAGE_SIZE) + 1;

	coremap = (struct pageitem *) PADDR_TO_KVADDR(paddrlow); 

	totalpages -= 1; // for safe measure lmao

	for (unsigned long i = 0; i < totalpages; i++)
	{

		if (i < (unsigned long) coremappages)
		{
			// coremap keeps track of the space occupied by itself 
			coremap[i].occupied = true;
		}
		else 
		{
			// initialization for the rest of the pages
			coremap[i].occupied = false; 
			coremap[i].blocksize = 1;
		}
	}
	spinlock_acquire(&stealmem_lock);
	memory_for_bootstrap = false;
	spinlock_release(&stealmem_lock);
	//kprintf("coremap address: %d\n", (int) coremap); // DEBUGGING
}

static
bool 
is_adequate_block(unsigned long i, unsigned long npages)
{
	for (unsigned long k = i; k < i + npages; k++)
	{
		if (coremap[i + k].occupied)
			return false;
	}
	return true;
}

static
void 
occupy_pages(unsigned long i, unsigned long npages)
{
	for (unsigned long k = 0; k < npages; k++)
	{
		if (!coremap[i + k].occupied)
		{
			coremap[i + k].occupied = true;
			coremap[i + k].blocksize = npages - k;
		}
		else 
		{
			panic("theres something wrong with your fucking is_adequate_block!");
		}
	}
}

static 
paddr_t
ram_borrowmem(unsigned long npages)
{
	// Find the first page with contiguity of at least (npages - 1) 
	for (unsigned long i = 0; i < totalpages; i++)
	{
		if (coremap[i].occupied == false && is_adequate_block(i, npages))
		{
			//kprintf("CORE PAGE FOUND AT %lu , %lu \n", i, (unsigned long) (paddrlow + (i * PAGE_SIZE))); // DEBUGGING
			occupy_pages(i, npages);
			return paddrlow + (i * PAGE_SIZE);
		}
		
	}
	panic("WE RAN OUT OF MEMORY ????? ");
	// it should not return 
	return 1;
}

//static 
//paddr_t 
//core_getppages(unsigned long npages)
//{
//	paddr_t addr; 
//	kprintf("GET CORE PAGES %lu \n", npages); // DEBUGGING
//	spinlock_acquire(&stealmem_lock);
//		addr = ram_borrowmem(npages);
//	spinlock_release(&stealmem_lock);
//	return addr;
//}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	//kprintf("GET P PAGES %lu , bootstrap = %d \n", npages, memory_for_bootstrap); // DEBUGGING

	spinlock_acquire(&stealmem_lock);
	if (memory_for_bootstrap)
		addr = ram_stealmem(npages);
	else 
		addr = ram_borrowmem(npages);
	spinlock_release(&stealmem_lock);
	return addr;
}

static
void
free_corepages(paddr_t addr)
{
	spinlock_acquire(&stealmem_lock);
	unsigned long index = (addr - paddrlow) / PAGE_SIZE; 
	
	//kprintf("FREE CORE PAGES AT %lu , %lu\n", index, (unsigned long) addr); // DEBUGGING

	coremap[index].occupied = 0; 
	for (unsigned long i = 1; i < coremap[index].blocksize; i++)
	{
		coremap[index + i].occupied = 0; 
		coremap[index + i].blocksize = 1;
	}
	//kprintf("%d pages freed.\n", coremap[index].blocksize); // DEBUGGING
	coremap[index].blocksize = 1;
	spinlock_release(&stealmem_lock);
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);

	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	spinlock_acquire(&stealmem_lock);
	if (memory_for_bootstrap)
	{
	spinlock_release(&stealmem_lock);
		/* nothing - leak the memory. */
	}
	else 
	{
	spinlock_release(&stealmem_lock);
		// new plan: let's NOT leak memory!
		
		// First: Convert to physical address 
		paddr_t paddr = addr - MIPS_KSEG0; // since vaddr = paddr + MIPS_KSEG0
		// Second: Free the pages in coremap
		free_corepages(paddr); 
	}

}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
		case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		//panic("dumbvm: got VM_FAULT_READONLY\n"); // RULE #1: DON'T PANIC :^) 
		//int exitstuffs = _MKWAIT_SIG(curproc->p_exitcode);	
		//sys__exit(_MKWAIT_SIG(VM_FAULT_READONLY));
		//kill_curthread()
		//kprintf("!!!!!!!!!!VM FAULT READONLY!!!!!!!!");
		//sys__exit(EX_MOD); // try this
		sys__exit(__WSIGNALED);
		break;

		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pttext != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_ptdata != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_ptstack != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	for (size_t i = 0; i < as->as_npages1; i++) //pagetables
		KASSERT((as->as_pttext[i] & PAGE_FRAME) == as->as_pttext[i]);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	for (size_t i = 0; i < as->as_npages2; i++) //pagestables
		KASSERT((as->as_ptdata[i] & PAGE_FRAME) == as->as_ptdata[i]);
	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++) //pagetables
		KASSERT((as->as_ptstack[i] & PAGE_FRAME) == as->as_ptstack[i]);
	
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	// Page Table Translation! 
	int pagenumber; 
	int offset;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		//paddr = (faultaddress - vbase1) + as->as_pbase1;
		pagenumber = (faultaddress - vbase1) / PAGE_SIZE;
        	offset = (faultaddress - vbase1) % PAGE_SIZE;
		paddr = as->as_pttext[pagenumber] + offset; 
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		//paddr = (faultaddress - vbase2) + as->as_pbase2;
		pagenumber = (faultaddress - vbase2) / PAGE_SIZE;
        	offset = (faultaddress - vbase2) % PAGE_SIZE;
		paddr = as->as_ptdata[pagenumber] + offset; 
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		pagenumber = (faultaddress - stackbase) / PAGE_SIZE;
        	offset = (faultaddress - stackbase) % PAGE_SIZE;
		//paddr = (faultaddress - stackbase) + as->as_stackpbase;
		paddr = as->as_ptstack[pagenumber] + offset;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);

		if (as->load_elfed == 1 && faultaddress >= vbase1 && faultaddress < vtop1)
			elo &= ~TLBLO_DIRTY; // Load TLB entries with TLBLO_DIRTY off (read-only)

		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	// No more empty TLB entries!! 
	// If the TLB is full, call tlb_random to write the entry into a random TLB slot. 
	ehi = faultaddress; 
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID; 

	if (as->load_elfed == 1 && faultaddress >= vbase1 && faultaddress < vtop1)
		elo &= ~TLBLO_DIRTY;// Load TLB entries with TLBLO_DIRTY off (read only)

	tlb_random(ehi, elo);
	splx(spl);
	//return 0; 

	// This part should NEVER run
	//kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	//splx(spl);
	return EFAULT;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	// Page Table (pt) Modifications. 
	as->as_vbase1 = 0;
	as->as_pttext = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_ptdata = 0;
	as->as_npages2 = 0;
	as->as_ptstack = 0;

	as->load_elfed = 0; // muh load elf

	return as;
}

void
as_destroy(struct addrspace *as)
{
	//free_kpages(as->as_vbase1); 
	//free_kpages(as->as_vbase2);	
	
	//free_kpages(PADDR_TO_KVADDR(as->as_pbase1)); 
	//free_kpages(PADDR_TO_KVADDR(as->as_pbase2)); 
	//free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
	
	// Call free_kpages on the frames for each segment
	for (size_t i = 0; i < as->as_npages1; i++)
	{
		free_kpages(PADDR_TO_KVADDR(as->as_pttext[i]));
	}
	for (size_t i = 0; i < as->as_npages2; i++)
	{
		free_kpages(PADDR_TO_KVADDR(as->as_ptdata[i])); 
	}
	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++)
	{
		free_kpages(PADDR_TO_KVADDR(as->as_ptstack[i])); 
	}
	
	// kfree the page tables 
	kfree(as->as_pttext); 
	kfree(as->as_ptdata);
	kfree(as->as_ptstack);

	// kfree addrspace
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable; // OPTIONAL - i.e. you don't have to do it!
	(void)writeable;
	(void)executable;

	// Allocate (kmalloc) and initialize the page table for the segment
	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->as_pttext = kmalloc(sizeof(paddr_t) * npages);
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_ptdata = kmalloc(sizeof(paddr_t) * npages);
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	//KASSERT(as->as_pttext == 0);
	//KASSERT(as->as_ptdata == 0);
	//KASSERT(as->as_ptstack == 0);
	
	// Pre-allocate frames for each page in the segments 
	for (size_t i = 0; i < as->as_npages1; i++)
	{
		as->as_pttext[i] = getppages(1); 
		if (as->as_pttext[i] == 0)
			return ENOMEM;
		as_zero_region(as->as_pttext[i], 1); //zero one region at a time
	}
	//as->as_pbase1 = getppages(as->as_npages1);
	//if (as->as_pbase1 == 0) {
	//	return ENOMEM;
	//}
	
	for (size_t i = 0; i < as->as_npages2; i++)
	{
		as->as_ptdata[i] = getppages(1);
		if (as->as_ptdata[i] == 0)
			return ENOMEM;
		as_zero_region(as->as_ptdata[i], 1); //zero one region at a time
	}
	//as->as_pbase2 = getppages(as->as_npages2);
	//if (as->as_pbase2 == 0) {
	//	return ENOMEM;
	//}

	// NOTE: as_define_region not called for the stack segment
	//       => Need to create a page table for the stack 
	as->as_ptstack = kmalloc(sizeof(paddr_t) * DUMBVM_STACKPAGES);

	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++)
	{
		as->as_ptstack[i] = getppages(1);
		if (as->as_ptstack[i] == 0)
			return ENOMEM;
		as_zero_region(as->as_ptstack[i], 1); //zero one region at a time
	}
	//as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	//if (as->as_stackpbase == 0) {
	//	return ENOMEM;
	//}
	
	// New as_zero_region for new pbases
	//as_zero_region(as->as_pbase1, as->as_npages1);
	//as_zero_region(as->as_pbase2, as->as_npages2);
	//as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);
	
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++)
		KASSERT(as->as_ptstack[i] != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	new->as_pttext = kmalloc(sizeof(paddr_t) * old->as_npages1);
	new->as_ptdata = kmalloc(sizeof(paddr_t) * old->as_npages2); 
	//new->as_pstack = kmalloc();

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pttext != 0);
	KASSERT(new->as_ptdata != 0);
	KASSERT(new->as_ptstack != 0);

	// Move one page at a time
	for (size_t i = 0; i < new->as_npages1; i++)
	{
		memmove((void *)PADDR_TO_KVADDR(new->as_pttext[i]),
			(const void *)PADDR_TO_KVADDR(old->as_pttext[i]),
			PAGE_SIZE);
	}

	for (size_t i = 0; i < new->as_npages2; i++)
	{
		memmove((void *)PADDR_TO_KVADDR(new->as_ptdata[i]),
			(const void *)PADDR_TO_KVADDR(old->as_ptdata[i]),
			PAGE_SIZE);
	}

	for (size_t i = 0; i < DUMBVM_STACKPAGES; i++)
	{
		memmove((void *)PADDR_TO_KVADDR(new->as_ptstack[i]),
			(const void *)PADDR_TO_KVADDR(old->as_ptstack[i]),
			PAGE_SIZE);
	}
	*ret = new;
	return 0;
}
