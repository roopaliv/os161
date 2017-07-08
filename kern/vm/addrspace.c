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
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <spl.h>
#include <mips/tlb.h>



struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	as->first = kmalloc(sizeof(struct first_level_page_table));
	if (as->first == NULL) {
		kfree(as);
		return NULL;
	}

	for (int i = 0; i < 1024; ++i) {
		as->first->second_levels[i] = NULL;
	}

	as->regions = NULL;
	as->heap = NULL;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}

	if (old->regions != NULL) {
		newas->regions = kmalloc(sizeof(struct region));
		if (newas->regions == NULL) {
			return ENOMEM;
		}
		*(newas->regions) = *(old->regions);
		struct region *new_region = newas->regions;
		struct region *old_region = old->regions->next_region;
		while (old_region != NULL) {
			new_region->next_region = kmalloc(sizeof(struct region));
			if (new_region->next_region == NULL) {
				kfree(newas->regions);
				return ENOMEM;
			}
			new_region = new_region->next_region;
			*(new_region) = *(old_region);
			old_region = old_region->next_region;
		}
		new_region->next_region = NULL;
	}
	if (old->heap != NULL) {
		newas->heap = kmalloc(sizeof(struct region));
		if (newas->heap == NULL) {
			return ENOMEM;
		}
		*(newas->heap) = *(old->heap);
	}

	for (unsigned i = 0; i < 1024; ++i) {
		struct second_level_page_table *pt = (old->first)->second_levels[i];
		if (pt == NULL) {
			(newas->first)->second_levels[i] = NULL;
		} else {
			(newas->first)->second_levels[i] = kmalloc(sizeof(struct second_level_page_table));
			if ((newas->first)->second_levels[i] == NULL) 
				return ENOMEM;
			for (int j = 0; j < 1024; ++j) {
				struct page_table_entry *pte = pt->actual_pages[j];
				if (pte == NULL) {
					((newas->first)->second_levels[i])->actual_pages[j] = NULL;
				} else {
					struct page_table_entry *new_pte = kmalloc(sizeof(struct page_table_entry));
					if (new_pte == NULL) {
						return ENOMEM;
					}
					if (pte->is_valid && pte->base != 0) {
						paddr_t new_pa = 0;
						new_pa = allocate_one_page(1); //user page
						if (new_pa == 0)
							return ENOMEM;
						new_pte->base = new_pa;
						memmove((void *) PADDR_TO_KVADDR(new_pte->base),
								(const void *) PADDR_TO_KVADDR(pte->base),
								PAGE_SIZE);
					}
					((newas->first)->second_levels[i])->actual_pages[j] = new_pte;
					new_pte->is_valid = pte->is_valid;
				}
			}
		}
	}

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	for (unsigned i = 0; i < 1024; ++i) {
		struct second_level_page_table *pt = (as->first)->second_levels[i];
		if (pt != NULL) {
			for (int j = 0; j < 1024; ++j) {
				struct page_table_entry *pte = pt->actual_pages[j];
				if (pte != NULL) {
					if (pte->is_valid && pte->base != 0) {
						free_kpages(PADDR_TO_KVADDR(pte->base));
					}
					kfree(pte);
				}
			}
			kfree(pt);
		}
	}

	struct region *curr = as->regions;
	while (curr != NULL) {
		struct region *temp = curr;
		curr = curr->next_region;
		kfree(temp);
	}

	kfree(as->heap);
	kfree(as->first);
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	vm_tlbshootdown_all();
}

void
as_deactivate(void)
{
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
				 int readable, int writeable, int executable)
{
	(void) readable;
	(void) writeable;
	(void) executable;
	size_t npages;

	memsize += vaddr & ~(vaddr_t) PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = memsize / PAGE_SIZE;
	struct region *new_region = kmalloc(sizeof(struct region));
	new_region->next_region = NULL;
	if (as->regions == NULL) {
		as->regions = new_region;
		as->heap = kmalloc(sizeof(struct region));
		as->heap->npages = 1;
		as->heap->next_region = NULL;
		as->heap->reg_start = 0;
		as->heap->reg_end = 0;
	} else {
		struct region *curr = as->regions;
		while (curr->next_region != NULL) {
			curr = curr->next_region;
		}
		curr->next_region = new_region;
	}
	new_region->npages = npages;

	new_region->reg_start = vaddr;
	new_region->reg_end = vaddr + npages * PAGE_SIZE;
	if (new_region->reg_end > as->heap->reg_start) {
		as->heap->reg_start = as->heap->reg_end = new_region->reg_end + PAGE_SIZE;
	}

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	struct region *curr = as->regions;
	while (curr != NULL) {
		if (alloc_region(as->first, curr->reg_start, curr->npages, POSITIVE)) { 
			return ENOMEM;
		}
		curr = curr->next_region;
	}

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	struct region *curr = as->regions;
	while (curr != NULL) {
		alloc_region(as->first, curr->reg_start, curr->npages, POSITIVE);
		curr = curr->next_region;
	}
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	as->stack_top = USERSTACK;
	as->stack_bottom = USERSTACK - 3000 * PAGE_SIZE;
	*stackptr = USERSTACK;
	return 0;
}


struct page_table_entry *find_pte(struct first_level_page_table *first, vaddr_t vaddr)
{
	struct second_level_page_table *pt = first->second_levels[(vaddr) >> 22];
	if (pt == NULL) return NULL;
	struct page_table_entry *pte = pt->actual_pages[(vaddr & 0x003FFFFF) >> 12];
	return pte;
}

void dealloc_pt(struct first_level_page_table *first, vaddr_t vaddr)
{
	struct page_table_entry *pte = find_pte(first, vaddr);
	if (pte == NULL) return;
	if (pte->is_valid) {
		free_kpages(PADDR_TO_KVADDR(pte->base));
	}
	first->second_levels[(vaddr) >> 22] = NULL;
	kfree(pte);
}

int alloc_region(struct first_level_page_table *first, vaddr_t vaddr, size_t npages, enum direction_alloc direction)
{
	vaddr_t curr = vaddr;
	for (size_t i = 0; i < npages; ++i) {
		struct second_level_page_table *pt = first->second_levels[(curr) >> 22];
		if (pt == NULL) {
			pt = kmalloc(sizeof(struct second_level_page_table));
			if (pt == NULL) {
				return ENOMEM;
			}
			for (int j = 0; j < 1024; ++j) {
				pt->actual_pages[j] = NULL;
			}
			first->second_levels[curr >> 22] = pt;
		}

		struct page_table_entry *pte = pt->actual_pages[(curr & 0x003FFFFF) >> 12];
		if (pte == NULL) {
			pte = kmalloc(sizeof(struct page_table_entry));
			if (pte == NULL) {
				return ENOMEM;
			}
			pt->actual_pages[(curr & 0x003FFFFF) >> 12] = pte;
			pte->is_valid = 0;
			pte->base = 0;
		}
		if(direction == POSITIVE)
			curr += PAGE_SIZE;
		else
			curr -= PAGE_SIZE;

	}
	return 0;
}