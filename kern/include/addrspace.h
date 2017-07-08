/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *  The President and Fellows of Harvard College.
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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>

struct vnode;

struct region {
  vaddr_t reg_start;
  vaddr_t reg_end;
  size_t npages;
  struct region *next_region;
};

struct addrspace {
#if OPT_DUMBVM
  vaddr_t as_vbase1;
  paddr_t as_pbase1;
  size_t as_npages1;
  vaddr_t as_vbase2;
  paddr_t as_pbase2;
  size_t as_npages2;
  paddr_t as_stackvbase;
#else
  struct region *regions;
  struct first_level_page_table* first;
  struct region *heap;
  vaddr_t stack_top;
  vaddr_t stack_bottom;
#endif
};

struct page_table_entry {
  paddr_t base;
  unsigned is_valid:1;

};

struct second_level_page_table {
  struct page_table_entry *actual_pages[1024];
};

struct first_level_page_table {
  struct second_level_page_table *second_levels[1024];
};
enum direction_alloc {  POSITIVE, NEGATIVE } ;

struct addrspace *as_create(void);

int as_copy(struct addrspace *src, struct addrspace **ret);

void as_activate(void);

void as_deactivate(void);

void as_destroy(struct addrspace *);

int as_define_region(struct addrspace *as,
           vaddr_t vaddr, size_t sz,
           int readable,
           int writeable,
           int executable);

int as_prepare_load(struct addrspace *as);

int as_complete_load(struct addrspace *as);

int as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

int load_elf(struct vnode *v, vaddr_t *entrypoint);

int alloc_region(struct first_level_page_table *first, vaddr_t vaddr, size_t npages, enum direction_alloc direction);

void dealloc_pt(struct first_level_page_table *first, vaddr_t vaddr);

struct page_table_entry *find_pte(struct first_level_page_table *first, vaddr_t vaddr);




#endif 
