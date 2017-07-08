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

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

	lock->lk_thread = NULL;
	spinlock_init(&lock->lk_lock);
	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lk_thread == NULL);
	spinlock_cleanup(&lock->lk_lock);
	wchan_destroy(lock->lk_wchan);
	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	KASSERT(lock != NULL);

	/* May not block in an interrupt handler.
	 * For robustness, always check, even if we can actually complete the lock acquire without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);
	spinlock_acquire(&lock->lk_lock);
	while (lock->lk_thread != NULL) {
		wchan_sleep(lock->lk_wchan, &lock->lk_lock);
	}
	lock->lk_thread = curthread;
	spinlock_release(&lock->lk_lock);
}

void
lock_release(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lk_thread == curthread);
	spinlock_acquire(&lock->lk_lock);
	lock->lk_thread = NULL;
	wchan_wakeone(lock->lk_wchan, &lock->lk_lock);
	spinlock_release(&lock->lk_lock);
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
}

bool
lock_do_i_hold(struct lock *lock)
{
	return lock->lk_thread == curthread; 
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}

	spinlock_init(&cv->cv_lock);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);
	kfree(cv->cv_name);
	spinlock_cleanup(&cv->cv_lock);
	wchan_destroy(cv->cv_wchan);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	spinlock_acquire(&cv->cv_lock);
	lock_release(lock);
    wchan_sleep(cv->cv_wchan, &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
	lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_lock);
	wchan_wakeone(cv->cv_wchan,  &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_lock);
	wchan_wakeall(cv->cv_wchan,  &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
}

////////////////////////////////////////////////////////////
//
// RW 


struct rwlock * 
rwlock_create(const char *name){
	struct rwlock *rwlock;

	rwlock = kmalloc(sizeof(*rwlock));
	if (rwlock == NULL) {
		return NULL;
	}

	rwlock->rwlock_name = kstrdup(name);
	if (rwlock->rwlock_name == NULL) {
		kfree(rwlock);
		return NULL;
	}

	rwlock->rwlock_reader_wchan = wchan_create(rwlock->rwlock_name);
	if (rwlock->rwlock_reader_wchan == NULL) {
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}
	rwlock->rwlock_writer_wchan = wchan_create(rwlock->rwlock_name);
	if (rwlock->rwlock_writer_wchan == NULL) {
		kfree(rwlock->rwlock_name);
		kfree(rwlock->rwlock_reader_wchan);
		kfree(rwlock);
		return NULL;
	}

	spinlock_init(&rwlock->rwlock_lock);

	rwlock->rwlock_noOfReaderThreads = 0;
    	rwlock->rwlock_hasWriterThread = false;
	rwlock->rwlock_noOfReaderThreads_waiting =0;
	return rwlock;
}
void rwlock_destroy(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	//KASSERT(rwlock->rwlock_noOfReaderThreads == 0);
	//KASSERT(rwlock->rwlock_hasWriterThread == false);
	spinlock_cleanup(&rwlock->rwlock_lock);
	wchan_destroy(rwlock->rwlock_reader_wchan);
	wchan_destroy(rwlock->rwlock_writer_wchan);
	kfree(rwlock->rwlock_name);
	kfree(rwlock);

}
void rwlock_acquire_read(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	spinlock_acquire(&rwlock->rwlock_lock);
	rwlock->rwlock_noOfReaderThreads_waiting++;

	while(rwlock->rwlock_hasWriterThread || (!wchan_isempty(rwlock->rwlock_writer_wchan, &rwlock->rwlock_lock) && rwlock->rwlock_noOfReaderThreads_waiting<20))
	//while(rwlock->rwlock_hasWriterThread || (!wchan_isempty(rwlock->rwlock_writer_wchan, &rwlock->rwlock_lock)))
		wchan_sleep(rwlock->rwlock_reader_wchan, &rwlock->rwlock_lock);
	rwlock->rwlock_noOfReaderThreads_waiting--;
	rwlock->rwlock_noOfReaderThreads++;
	spinlock_release(&rwlock->rwlock_lock);
}
void rwlock_release_read(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->rwlock_noOfReaderThreads > 0);
	spinlock_acquire(&rwlock->rwlock_lock);
	rwlock->rwlock_noOfReaderThreads--;
	//if(rwlock->rwlock_noOfReaderThreads==0)
	wchan_wakeone(rwlock->rwlock_writer_wchan, &rwlock->rwlock_lock);
	wchan_wakeall(rwlock->rwlock_reader_wchan, &rwlock->rwlock_lock);
	spinlock_release(&rwlock->rwlock_lock);
}
void rwlock_acquire_write(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	spinlock_acquire(&rwlock->rwlock_lock);
//we can check here if no of readers waiting are greater than say 10 maybe
	while(rwlock->rwlock_hasWriterThread || rwlock->rwlock_noOfReaderThreads>0 || rwlock->rwlock_noOfReaderThreads_waiting>=20)
		wchan_sleep(rwlock->rwlock_writer_wchan, &rwlock->rwlock_lock);

	rwlock->rwlock_hasWriterThread = true;
	spinlock_release(&rwlock->rwlock_lock);

}
void rwlock_release_write(struct rwlock *rwlock){
	// writer boolean should be true
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->rwlock_hasWriterThread);
	spinlock_acquire(&rwlock->rwlock_lock);
	// make writer boolean false
	rwlock->rwlock_hasWriterThread = false;
	// wake one from both write waitchannel and wake all from read channel
	//if (rwlock->rwlock_noOfReaderThreads_waiting<=20)
	wchan_wakeone(rwlock->rwlock_writer_wchan, &rwlock->rwlock_lock);
	
	wchan_wakeall(rwlock->rwlock_reader_wchan, &rwlock->rwlock_lock);
	spinlock_release(&rwlock->rwlock_lock);

}
