/**
 * @author weichbr
 */

#include "rwlock.h"
#include <cerrno>

#define atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define cmpxchg(P, O, N) __sync_val_compare_and_swap((P), (O), (N))
#define atomic_inc(P) __sync_add_and_fetch((P), 1)

/* Compile read-write barrier */
#define barrier() asm volatile("": : :"memory")

/* Pause instruction to prevent excess processor bus usage */
#define cpu_relax() asm volatile("pause\n": : :"memory")

void write_lock(rwlock_t *l)
{
	unsigned me = atomic_xadd(&l->u, (1<<16));
	unsigned char val = me >> 16;

	while (val != l->s.write) cpu_relax();
}

void write_unlock(rwlock_t *l)
{
	rwlock_t t = *l;

	barrier();

	t.s.write++;
	t.s.read++;

	*(unsigned short *) l = t.us;
}

int write_trylock(rwlock_t *l)
{
	unsigned me = l->s.users;
	unsigned char menew = me + 1;
	unsigned read = l->s.read << 8;
	unsigned cmp = (me << 16) + read + me;
	unsigned cmpnew = (menew << 16) + read + me;

	if (cmpxchg(&l->u, cmp, cmpnew) == cmp) return 0;

	return EBUSY;
}

void read_lock(rwlock_t *l)
{
	unsigned me = atomic_xadd(&l->u, (1<<16));
	unsigned char val = me >> 16;

	while (val != l->s.read) cpu_relax();
	l->s.read++;
}

void read_unlock(rwlock_t *l)
{
	atomic_inc(&l->s.write);
}

int read_trylock(rwlock_t *l)
{
	unsigned me = l->s.users;
	unsigned write = l->s.write;
	unsigned char menew = me + 1;
	unsigned cmp = (me << 16) + (me << 8) + write;
	unsigned cmpnew = ((unsigned) menew << 16) + (menew << 8) + write;

	if (cmpxchg(&l->u, cmp, cmpnew) == cmp) return 0;

	return EBUSY;
}
