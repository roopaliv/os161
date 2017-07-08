#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>

static uint32_t tlb_index = 0;
static struct spinlock tlb_lock;
static struct cm_listing *coremap;
static unsigned int coremap_start;
static unsigned int coremap_count;
static volatile unsigned int free_places;
static struct spinlock coremap_lock;


void vm_bootstrap(void)
{
	spinlock_init(&tlb_lock);
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{

	if (curproc == NULL) 
		return EFAULT;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) 
		return EFAULT;

	faultaddress &= PAGE_FRAME;
	struct region *curr = as->regions;
	bool belongs = false;
	while (curr != NULL) {
		if ((faultaddress >= curr->reg_start && faultaddress < curr->reg_end)) {
			belongs = true;
			break;
		}
		curr = curr->next_region;
	}
	if (!belongs && faultaddress >= as->heap->reg_start && faultaddress < as->heap->reg_end) {
		belongs = true;
	}
	enum direction_alloc direction = POSITIVE;
	if (!belongs && faultaddress >= (USERSTACK - 3000 * PAGE_SIZE) && faultaddress < USERSTACK) {
		direction = NEGATIVE;
		belongs = true;
	}
	if (!belongs) 
		return EFAULT;
	
	uint32_t ehi, elo;
	int spl;

	struct page_table_entry *pte = find_pte(as->first, faultaddress);
	if (pte == NULL) {
		if (alloc_region(as->first, faultaddress, 1, direction)) {
			return ENOMEM;
		}
	}
	pte = find_pte(as->first, faultaddress);
	if (!pte->is_valid) {
		pte->base = allocate_one_page(1);
		if (pte->base == 0) //user page
			return ENOMEM;
		pte->is_valid = 1;
	}

	(void) faulttype;
	
	spinlock_acquire(&tlb_lock);
	spl = splhigh();
	paddr_t paddr = (pte->base);
	ehi = faultaddress;
	elo = paddr | TLBLO_VALID| TLBLO_DIRTY;
	tlb_write(ehi, elo, tlb_index);
	tlb_index = (tlb_index + 1) % NUM_TLB;
	splx(spl);
	spinlock_release(&tlb_lock);

	return 0;
}

void vm_tlbshootdown_all(void)
{
	spinlock_acquire(&tlb_lock);
	int spl = splhigh();

	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
	spinlock_release(&tlb_lock);
}

void vm_tlbshootdown(const struct tlbshootdown *tlbs)
{
	(void) tlbs;
}

void cm_bootstrap(void)
{
	paddr_t last = ram_getsize();
	paddr_t first_free = ram_getfirstfree();
	unsigned int pages = (last - first_free) / PAGE_SIZE;
	coremap = (struct cm_listing *) PADDR_TO_KVADDR(first_free);
	first_free = first_free + ROUNDUP(pages * sizeof(struct cm_listing), PAGE_SIZE);
	coremap_start = first_free / PAGE_SIZE;
	coremap_count = free_places = ((last - first_free) / PAGE_SIZE);
	for (unsigned i = 0; i < coremap_count; i++) {
		coremap[i] = (struct cm_listing) {.page_count = 0, .state = FREE};
	}
	spinlock_init(&coremap_lock);
}

paddr_t allocate_page(unsigned int free_entry_index, int type)
{
	if(type == 0) //kernel page
		coremap[free_entry_index].state = FIXED;
	else
		coremap[free_entry_index].state = DIRTY;
	free_places--;
	paddr_t paddr = (coremap_start + free_entry_index) * PAGE_SIZE;
	bzero((void *) PADDR_TO_KVADDR(paddr), PAGE_SIZE);
	return paddr;
}

void free_page(unsigned int used_entry_index)
{
	free_places++;
	coremap[used_entry_index] = (struct cm_listing) {.page_count = 0, .state = FREE};
}

vaddr_t alloc_kpages(unsigned npages)
{
	paddr_t pa = 0;
	if (npages == 1) 
		pa = allocate_one_page(0); // kernel page
	 else if (npages > 1) 
		pa = allocate_multiple_pages(0, npages); // kernel page

	if (pa != 0) 
		return PADDR_TO_KVADDR(pa);
	return pa;
}


void free_kpages(vaddr_t addr)
{
	paddr_t paddr = (addr)-MIPS_KSEG0;
	unsigned int index =  (paddr / PAGE_SIZE) - coremap_start;
	if (index <= coremap_count) {
		unsigned int chunk_size = coremap[index].page_count;
		if (chunk_size > 0) {
			spinlock_acquire(&coremap_lock);
			if (chunk_size == 1) {
				free_page(index);
			} else {
				for (unsigned int i = index; i < index + chunk_size; ++i) 
					free_page(i);
			}
			spinlock_release(&coremap_lock);
		}
		else
			return;
	}
}

unsigned int coremap_used_bytes()
{
	int  occupied = coremap_count - free_places;
	return occupied * PAGE_SIZE;
}

paddr_t allocate_multiple_pages(int type, unsigned int npages)
{
	if (free_places > npages) {
		spinlock_acquire(&coremap_lock);
		unsigned int index = 0;
		unsigned int chunk_size = 0;
		for (unsigned int i = 0; i < coremap_count; i++) {
			if (chunk_size == npages) 
				break;
			if (chunk_size <= 0 && coremap[i].state == FREE) {
				index = i;
				chunk_size = 1;
			} else if (chunk_size > 0 && coremap[i].state == FREE) {
				chunk_size++;
			} else if (chunk_size > 0) {
				chunk_size = 0;
			}
		}
		if (chunk_size == npages) {
			for (unsigned int i = index; i < index + chunk_size; ++i) 
				allocate_page(i, type);
			coremap[index].page_count = chunk_size;
			spinlock_release(&coremap_lock);
			return (coremap_start + index) * PAGE_SIZE;
		}

		spinlock_release(&coremap_lock);
	}
	return 0;
}

paddr_t allocate_one_page(int type)
{
	paddr_t paddr = 0;
	if (free_places >= 1) {
		spinlock_acquire(&coremap_lock);
		int index = -1;
		for (unsigned i = 0; i < coremap_count; i++) {
			if (coremap[i].state == FREE) {
				index = i;
				break;
			}
		}
		if (index >= 0) {
			paddr = allocate_page((unsigned int) index, type);
			coremap[index].page_count = 1;
		}
		spinlock_release(&coremap_lock);
	}
	return paddr;
}

