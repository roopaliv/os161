#ifndef _FILEHANDLE_H_
#define _FILEHANDLE_H_
#include <vnode.h>
#include <uio.h>
#include <synch.h>
#include <types.h>


struct filehandle
{
	char *fh_name; //name
	off_t fh_offset;
	struct vnode *fh_vnode;
	int fh_refcount; 
	mode_t fh_mode; 
	int fh_flags; 
	struct lock* fh_lock;
};

struct filehandle * fh_create(const char *name, struct vnode *vnode);
int fh_assign(struct filehandle* fh);
void fh_destroy(struct filehandle *fh);
void fh_release(struct filehandle *fh, int fd);

#endif
