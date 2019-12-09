#include <iostream>
#include <cstdio>
#include "btree_lock.h"

int
btree_lock_init(btree_lock_t *lock) {
	if (!lock)
		return -1;
	lock->lock = 0;
	lock->holder = 0;
	return 0;
}

int
btree_lock_lock(btree_lock_t *lock) {

	while(true) {
		__asm__ __volatile__("pause");
		bool ret = __sync_bool_compare_and_swap(&lock->lock, 0, 1);
		if (ret)
			break;
	}

	return true;
}

int
btree_lock_unlock(btree_lock_t *lock) {
	int ret = __sync_bool_compare_and_swap(&lock->lock, 1, 0);
	return ret;
}
