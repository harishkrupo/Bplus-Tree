#ifndef _BTREE_LOCK_H_
#define _BTREE_LOCK_H_

typedef struct btree_lock
{
	volatile int lock;
	unsigned int holder;
} btree_lock_t;

int
btree_lock_init(btree_lock_t *l);

int
btree_lock_lock(btree_lock_t *l);

int
btree_lock_unlock(btree_lock_t *l);

#endif //_BTREE_LOCK_H_
