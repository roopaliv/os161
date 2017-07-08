
#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_


struct listStr{
  char *val;
  struct listStr *next;
};
int sys_getpid(pid_t *retVal);
pid_t generate_pid(struct proc* p);
void entry_point(void* data1, unsigned long data2);
int sys_fork(struct trapframe* parent_tf, int *retVal);
int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retVal);
int sys_execv(const char *progname, char **args);
void sys__exit(int exitcode);
void *sys_sbrk(intptr_t amount, vaddr_t *retval);
#endif
