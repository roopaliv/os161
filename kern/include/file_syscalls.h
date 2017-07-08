

#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_
#include <types.h>

size_t sys_write(int fd, const void *buf, size_t bufflen, int* retVal);
int sys_read(int fd, void *buf, size_t buflen, int *retVal);
int sys_open(const char *filename, int flags, mode_t mode, int *retVal);
int sys_close(int fd);
int sys_dup2(int oldfd, int newfd, int *retVal);
off_t sys_lseek(int fd, off_t pos, int whence, int *retVal, int *retVal2);
#endif
