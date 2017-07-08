#include <types.h>
#include <lib.h>
#include <filehandle.h>
#include <synch.h>
#include <vnode.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <limits.h>




struct filehandle * fh_create(const char *name, struct vnode *vnode){
	
	struct filehandle *fh;

	fh = kmalloc(sizeof(*fh));
	if (fh == NULL) {
		return NULL;
	}

	fh->fh_name = kstrdup(name);
	if (fh->fh_name == NULL) {
		kfree(fh);
		return NULL;
	}
	fh->fh_offset = 0;
	fh->fh_refcount = 0;
	fh->fh_vnode = vnode;
	fh->fh_lock = lock_create("fh");

	return fh;
}

// deallocate memory
void fh_destroy(struct filehandle *fh){
		KASSERT(fh != NULL);
		lock_destroy(fh->fh_lock);
		kfree(fh->fh_name);
		kfree(fh);
}

int fh_assign(struct filehandle* fh)
{
	lock_acquire(fh->fh_lock);
	for(int fd=3; fd<OPEN_MAX; fd++)
	{
		if(curproc->p_fileTable[fd] == NULL)
		{
			fh->fh_refcount++;
			curproc->p_fileTable[fd] = fh;
			lock_release(fh->fh_lock);
			return fd;
		}
	}
	lock_release(fh->fh_lock);
	return -1; 
}

//decrement no of processes - lock protected function
void fh_release(struct filehandle *fh, int fd){
	lock_acquire(fh->fh_lock);
	fh->fh_refcount--;
	curproc->p_fileTable[fd] = NULL;
	lock_release(fh->fh_lock);
	if(fh->fh_refcount==0)
		fh_destroy(fh); // internally checks refcount before destroying
}

