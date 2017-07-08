#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <array.h>
#include <limits.h>
#include <test.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <proc_syscalls.h>
#include <syscall.h>
#include <signal.h>
#include <mips/specialreg.h>
#include <cpu.h>
#include <spl.h>
#include <vm.h>
#include <mainbus.h>
#include <mips/tlb.h>
#include <spl.h>

struct proc* process_table[128];
pid_t current_pid = 0;
struct array *recycledPids;


int sys_getpid(pid_t *retVal)
{
  *retVal = curproc->p_pid;
  return 0;
}


pid_t generate_pid(struct proc* p){
	int pid = -1;
	lock_acquire(ptLock);
	for(int i=1; i<128;i++){
		if(process_table[i] == NULL){
			pid = i;
			process_table[pid] = p;
			break;
		}
	}
	lock_release(ptLock);
    return pid; 
}

int sys_fork(struct trapframe* parent_tf, int *retVal)
{

	int result = 0;
	struct proc *childProc = proc_createchild("childname");
	if(childProc==NULL){
		return ENOMEM;
	}
	childProc->p_pid = generate_pid(childProc);
	if(childProc->p_pid == -1)
		return EMPROC;
	//kprintf("childid:%d",childProc->p_pid);
	childProc->p_parentpid = curproc->p_pid;
	//kprintf("parent id:%d",curproc->p_pid);
	childProc->p_exitCode = -1; // not exited yet
	childProc->p_state = 1; //running
	//kprintf("1");		
	
	//copy address space
 	as_copy(curproc->p_addrspace,&childProc->p_addrspace);
	if(childProc->p_addrspace == NULL) {
	    //proc_destroy(childProc);
	    return ENOMEM;
	}


	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		childProc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);


	
	//copy current working directory
	//childProc->p_cwd = curproc->p_cwd;

	//copy file table
	int fd;
	for(fd = 0; fd<64; fd++){
		if(curproc->p_fileTable[fd] != NULL){
			childProc->p_fileTable[fd] = curproc->p_fileTable[fd];
			lock_acquire(childProc->p_fileTable[fd]->fh_lock);
			childProc->p_fileTable[fd]->fh_refcount++;
			lock_release(childProc->p_fileTable[fd]->fh_lock);
		}
	}

	//copy trapframe
  	struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
	if (child_tf == NULL)
  	{
    	//proc_destroy(childProc);
    	return ENOMEM;
  	}
 	memcpy(child_tf,parent_tf, sizeof(struct trapframe));
  	//*child_tf = *parent_tf;
    result = thread_fork("childname", childProc, enter_forked_process,child_tf, 0);
	if (result){
    	//proc_destroy(childProc);
		return ENOMEM;//result;
	}
	*retVal = childProc->p_pid;
	return 0;
}

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retVal)
{
	/*if (recycledPids == NULL) {
		recycledPids = array_create();
		array_init(recycledPids);
	}*/

/*
	badcall: siblings wait for each other... 
   from child:              No such process                             passed
   sibling (pid 9)                       No such process                passed
   sibling (pid 8)                       No child processes             passed
   overall                               
   OOPS: error waiting for child 1 (pid 9): No such process

*/
	int exitCode;
	int result = 0;
	if(pid<0 || pid>PID_MAX)
		return ESRCH;
	lock_acquire(ptLock);
	struct proc *p = process_table[pid];
	lock_release(ptLock);
	if(p == NULL) 
    		return ESRCH;
  	if(curproc == p) //dont wait on urself 
    		return ECHILD;

  	if(curproc->p_pid != p->p_parentpid) {
    		return ECHILD;
    }
  	if (options != 0) 
    		return(EINVAL);

    P(p->p_exitSem);
	//kprintf("\nwaiting complete, my pid: %d and child pid: %d \n",curproc->p_pid,pid);
	exitCode = p->p_exitCode;
	p->p_state = 0;
	//proc_destroy(p);
	
	if(status != NULL){
		result = copyout((void *)&exitCode,status,sizeof(int));
		if (result) 
			return EFAULT;
	}
	//kprintf("\nstatus for id %d is %d after wait\n", p->p_pid, exitCode);
	lock_acquire(ptLock);
	process_table[pid]=NULL;
	lock_release(ptLock);
	
	//if(p->p_exitSem != NULL)
	//sem_destroy(p->p_exitSem);
	struct addrspace *as;
	as = p->p_addrspace;
	p->p_addrspace = NULL;
	as_destroy(as);
	
	for (int i = 0; i < 64; i++) {
		struct filehandle *fh=p->p_fileTable[i] ;
		if(fh != NULL){
			p->p_fileTable[i] = NULL;
			lock_acquire(fh->fh_lock);
			fh->fh_refcount--;
			lock_release(fh->fh_lock);

			if (fh->fh_refcount==0)
				fh_destroy(fh);
		}
	}

	spinlock_acquire(&curproc->p_lock);
	if (p->p_cwd ) {
		VOP_DECREF(p->p_cwd);
		p->p_cwd = NULL;
	}
	spinlock_release(&curproc->p_lock);
	spinlock_cleanup(&p->p_lock);
	kfree(p->p_name);

	*retVal = pid;

	//kfree(p);
	return 0;
}
void sys__exit(int exitcode) {
	struct proc *p = curproc;
    p->p_exitCode = _MKWAIT_EXIT(exitcode); 
    V(p->p_exitSem);
	thread_exit();
	
}
int sys_execv(const char *progname, char **args)
{

	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	
	int argc = 0;

	char* kernel_progname = kmalloc(PATH_MAX);
	result=copyinstr((userptr_t) progname, kernel_progname, PATH_MAX, NULL);
	if(result)
	{
		kfree(kernel_progname);
		kfree(args);
		return result;
	}
	//kfree((void *)progname);
	char **kernel_args=kmalloc(4*sizeof(char **));
	result = copyin((const_userptr_t) args, kernel_args, 4*sizeof(char **));
	if(result){
		kfree(kernel_progname);
		kfree(kernel_args);
		kfree(args);
		return result;
	}
	kfree(kernel_args);
	
	if(args==NULL){
		return EFAULT;
	}
	char* strMain = kmalloc(ARG_MAX);
	while (args[argc] != NULL){
		result = copyinstr((userptr_t) args[argc], strMain, ARG_MAX, NULL); 
		if(result){
			kfree(kernel_progname);
			//kfree(kernel_args);
			kfree(args);
			kfree(strMain);
			return result;
		}
		argc++;
	}
	kfree(strMain);
	int i;

	struct listStr* MyArgs = kmalloc(sizeof(struct listStr));
	struct listStr* startPointerArgs = MyArgs;
	for (i = 0; args[i]; i++){	
		size_t len = 0;
		int ActualLength = 0;
		if(args[i] != NULL){
			ActualLength =  strlen(args[i]);
		}
		else{
			kfree(kernel_progname);
			//kfree(kernel_args);
			kfree(args);
			return EFAULT;
		}

		int padding = 0;
		padding = 4 - (ActualLength%4);
		size_t paddedLen= ActualLength+padding;
		char* str = kmalloc(paddedLen * sizeof(char));
	
		result = copyinstr((userptr_t) args[i], str, paddedLen, &len); 
		if(result){
			kfree(str);
			kfree(kernel_progname);
			//kfree(kernel_args);
			kfree(args);
			return result;
		}

		MyArgs->val = str;
		if(i!=argc-1){
			MyArgs->next = kmalloc(sizeof(struct listStr));
			MyArgs = MyArgs->next;
		}
		else{
			MyArgs->next = NULL;
		}

  	}

	result = vfs_open((char *)kernel_progname, O_RDONLY, 0, &v);
	if (result) {
		kfree(kernel_progname);
		//kfree(kernel_args);
		kfree(args);
		return result;
	}
	as = as_create();
	
	if (as == NULL) {
		vfs_close(v);
		kfree(kernel_progname);
		//kfree(kernel_args);
		kfree(args);
		return ENOMEM;
	}
	struct addrspace *prev_as = proc_setas(as);
	as_activate();

	result = load_elf(v, &entrypoint);
	if (result) {
		vfs_close(v);
		kfree(kernel_progname);
		//kfree(kernel_args);
		kfree(args);
		return result;
	}

	vfs_close(v);
		result = as_define_stack(curproc->p_addrspace, &stackptr);
	if (result) {
		kfree(kernel_progname);
		//kfree(kernel_args);
		kfree(args);
		return result;
	}
	vaddr_t startOfStrings = stackptr;
	MyArgs = startPointerArgs;
	while (MyArgs!=NULL){
		int lengthPadded = strlen(MyArgs->val);
		int padding = 0;
		padding = 4 - (lengthPadded%4);
		lengthPadded += padding;
		stackptr -= lengthPadded;
		size_t got;
		result = copyoutstr((const void *) MyArgs->val, (userptr_t) stackptr, lengthPadded, &got);
		if (result){
			kfree(kernel_progname);
			//kfree(kernel_args);
			kfree(args);
			return result;
		}  
		MyArgs = MyArgs->next;
	}

	stackptr -= 4 * sizeof(char);

	vaddr_t endOfPointers = stackptr - (argc* sizeof(vaddr_t *));
	vaddr_t CurrentPtr = endOfPointers;
	MyArgs = startPointerArgs;
	while (MyArgs!=NULL){
		int lengthPadded = strlen(MyArgs->val);
		int padding = 0;
		padding = 4 - (lengthPadded%4);
		lengthPadded +=padding;
		startOfStrings -= lengthPadded;
		result = copyout((const void *) &startOfStrings, (userptr_t) CurrentPtr, sizeof(vaddr_t));
		if (result){
			kfree(kernel_progname);
			//kfree(kernel_args);
			kfree(args);
			return result;
		}
		CurrentPtr = CurrentPtr + sizeof(vaddr_t *);
		MyArgs = MyArgs->next;
	}

	stackptr = endOfPointers;

	struct listStr *cur, *prev;
    cur = prev = startPointerArgs;
    while (cur != NULL) {
        cur = cur->next;
        kfree(prev->val);
        kfree(prev);
        prev = cur;
    }
    if (prev != NULL) {
        kfree(prev->val);
        kfree(prev);
    }
	if (prev_as) {
		as_destroy(prev_as);
	}
	//kfree(args);
	enter_new_process(argc , (userptr_t)stackptr ,  NULL ,  stackptr, entrypoint);
	panic("enter_new_process returned\n");
	return EINVAL;
}

void *
sys_sbrk(intptr_t amount, vaddr_t *retval){
	
	struct addrspace *as = proc_getas();
	*retval = as->heap->reg_end;

	if (amount == 0) 
		return (void *)0;

	vaddr_t new = (as->heap->reg_end + (vaddr_t) amount) & PAGE_FRAME;

	if (new < as->heap->reg_start || (amount <= (-4096 * 1024 * 256)))  
		return (void *)EINVAL;
	if (new >= (USERSTACK - 3000 * PAGE_SIZE) || new > USERSPACETOP) 
		return (void *)ENOMEM;

	if (new < as->heap->reg_end) {
		int size = ((as->heap->reg_end - new) & PAGE_FRAME) / PAGE_SIZE;
		for (int j = 0; j < size; ++j) {
			vaddr_t free = new + j * PAGE_SIZE;
			struct page_table_entry *pte = find_pte(as->first, free);
			if (pte != NULL) {
				if (pte->is_valid) {
					vaddr_t pvaddr = PADDR_TO_KVADDR(pte->base);
					free_kpages(pvaddr);
					pte->is_valid = 0;
					pte->base = 0;
					int spl = splhigh();
					int i = tlb_probe(free, 0);
					if (i >= 0) {
						tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
					}
					splx(spl);
				}
			}
		}
	}
	as->heap->reg_end = new;
	return (void *)0;
}
/*
void *
sys_sbrk(intptr_t amount, vaddr_t *retval){
	(void) amount;
	retval = 0;
	return (void *) retval;
}
*/	