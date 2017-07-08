#include <file_syscalls.h>
#include <syscall.h>
#include <types.h>
#include <copyinout.h>
#include <vnode.h>
#include <uio.h>
#include <filehandle.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/seek.h>
#include <kern/stat.h>


size_t sys_write(int fd, const void *buf, size_t bufflen, int* retVal)
{
	int result = 0;
	if(fd < 0 || fd >= OPEN_MAX)
		return EBADF;
	if(curproc->p_fileTable[fd] == NULL) 
		return EBADF;

	if (!((curproc->p_fileTable[fd]->fh_flags & O_WRONLY) || (curproc->p_fileTable[fd]->fh_flags & O_RDWR)))
		return EBADF;
	struct filehandle *fh = curproc->p_fileTable[fd];

	struct vnode *vn;
  	lock_acquire(fh->fh_lock);

	vn = (fh->fh_vnode);
	
	struct iovec uio_iovec;
	struct uio uio;


	uio_iovec.iov_kbase = (userptr_t) buf; 
	uio_iovec.iov_len = bufflen;
	uio.uio_iov = &uio_iovec;
	uio.uio_iovcnt = 1;
	uio.uio_offset= fh->fh_offset;
	uio.uio_resid = bufflen;
	uio.uio_segflg=UIO_USERSPACE;
	uio.uio_rw=UIO_WRITE;
	uio.uio_space = curproc->p_addrspace;

    result = VOP_WRITE(vn,&uio);
	if(result){
    	result = EFAULT;
		lock_release(fh->fh_lock);
    	return result;
	}
	fh->fh_offset = uio.uio_offset;
    *retVal = bufflen - uio.uio_resid; 
    lock_release(fh->fh_lock);
    return result;
}

int sys_read(int fd, void *buf, size_t buflen, int *retVal)
{

	int result=0;
	if(fd >= OPEN_MAX || fd < 0) 
		return EBADF;
	if(curproc->p_fileTable[fd] == NULL) 
		return EBADF;

	if (curproc->p_fileTable[fd]->fh_flags & O_WRONLY) 
		return EBADF;
	struct vnode *vn;
	struct filehandle *fh = curproc->p_fileTable[fd];

	lock_acquire(fh->fh_lock);
	vn = fh->fh_vnode;
	struct iovec uio_iovec;
	struct uio uio;

	uio_iovec.iov_ubase = (userptr_t)buf;
	uio_iovec.iov_len = buflen;
	uio.uio_iov = &uio_iovec;
	uio.uio_iovcnt = 1;
	uio.uio_offset = fh->fh_offset;
	uio.uio_resid = buflen;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_space = curproc->p_addrspace;

	result = VOP_READ(vn, &uio);
	if(result) {
		lock_release(fh->fh_lock);
		return result;
	}
	fh->fh_offset = uio.uio_offset;
	*retVal = buflen - uio.uio_resid;
	lock_release(fh->fh_lock);
	return 0;
}

int sys_open(const char *filename, int flags, mode_t mode,  int *retVal){
	struct vnode* open_vnode;
	char kernelname[PATH_MAX];
	//kprintf("flags %d", flags);
  	int result=0;
	size_t len;
	result = copyinstr((userptr_t)filename,kernelname,PATH_MAX,&len);  
	if(result){
		return result;
		kfree(kernelname);
	}

	result = vfs_open(kernelname, flags, 0, &open_vnode);
	if (result)
		return result;

	struct filehandle *fhOpen;
	fhOpen = fh_create("openFile", open_vnode);
	if (fhOpen == NULL) 
		return ENOMEM;
	lock_acquire(fhOpen->fh_lock);
	fhOpen->fh_flags = flags;
	fhOpen->fh_mode = mode;
	fhOpen->fh_refcount = 1;
	fhOpen->fh_offset = 0;
	lock_release(fhOpen->fh_lock);

	*retVal = fh_assign(fhOpen);
	if(*retVal == -1){
		fh_destroy(fhOpen);
		return EMFILE;
	}
	return 0;
}

int sys_close(int fd) {
	if(fd >= OPEN_MAX || fd < 0) 
		return EBADF;
	struct filehandle *fh = curproc->p_fileTable[fd];
	if(fh == NULL) 
		return EBADF;
	if(fh== NULL) 
		return EBADF;
  	lock_acquire(fh->fh_lock);
	if(fh->fh_refcount==0){
		VOP_DECREF(fh->fh_vnode);
	}
  	lock_release(fh->fh_lock);
	fh_release(fh, fd);
	return 0;

}


int sys_dup2(int oldfd, int newfd, int *retVal) {

	int result = 0;

	if(oldfd >= OPEN_MAX || newfd >= OPEN_MAX || oldfd < 0 || newfd < 0) 
		return EBADF;

	struct filehandle *oldfh = curproc->p_fileTable[oldfd];
	if(oldfh == NULL) {
		return EBADF;
	}

	if(oldfd == newfd) {
		*retVal = newfd;
		return result;
	}

	if(curproc->p_fileTable[newfd] != NULL) {
		result = sys_close(newfd);
		if(result)
			return result;
	}

	
	lock_acquire(oldfh->fh_lock);
	struct filehandle *tempFileHandle = fh_create("openFile", oldfh->fh_vnode);
	lock_release(oldfh->fh_lock);
	lock_acquire(tempFileHandle->fh_lock);
	curproc->p_fileTable[newfd] = tempFileHandle;
	tempFileHandle->fh_refcount++;
	lock_release(tempFileHandle->fh_lock);
	
	struct filehandle *newfh = curproc->p_fileTable[newfd];

	lock_acquire(oldfh->fh_lock);
	newfh->fh_offset = oldfh->fh_offset;
	newfh->fh_flags = oldfh->fh_flags;
	newfh->fh_mode = oldfh->fh_mode;
	lock_release(oldfh->fh_lock);
	*retVal = newfd;
	return 0;
}


off_t sys_lseek(int fd, off_t pos, int whence, int *retVal, int *retVal2) {
	int result = 0;
	if(fd >= OPEN_MAX || fd < 0) 
		return EBADF;

	if(curproc->p_fileTable[fd] == NULL) 
		return EBADF;

	if(!VOP_ISSEEKABLE(curproc->p_fileTable[fd]->fh_vnode))
		return ESPIPE;

	struct stat static_buffer;

	lock_acquire(curproc->p_fileTable[fd]->fh_lock);
	result = VOP_STAT(curproc->p_fileTable[fd]->fh_vnode, &static_buffer);
	if(result) {
		lock_release(curproc->p_fileTable[fd]->fh_lock);
		return result;
	}

	off_t size = static_buffer.st_size;
	off_t offset;
	//SEEK_SET, the new position is pos.
	//SEEK_CUR, the new position is the current position plus pos.
	//SEEK_END, the new position is the position of end-of-file plus pos. 
	//anything else, lseek fails.
	if(whence == SEEK_SET) {
		offset = pos;

	}else if(whence == SEEK_CUR) {
		offset = curproc->p_fileTable[fd]->fh_offset + pos;

	}else if(whence == SEEK_END) {
		offset = size + pos;

	}else {
		lock_release(curproc->p_fileTable[fd]->fh_lock);
		return EINVAL;
	}

	if(offset < (off_t)0) {
		lock_release(curproc->p_fileTable[fd]->fh_lock);
		return EINVAL;
	}
	curproc->p_fileTable[fd]->fh_offset = offset;
	*retVal = (uint32_t)(offset >> 32);
	*retVal2 = (uint32_t)(offset & 0xffffffff);
	lock_release(curproc->p_fileTable[fd]->fh_lock);
	return 0;
}



