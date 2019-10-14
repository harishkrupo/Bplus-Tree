#ifndef _BTREE_H_
#define _BTREE_H_

struct BTree;

enum BTREE_RETURN {
		  BTREE_RETURN_SUCCESS = 0,
		  BTREE_RETURN_KEY_PRESENT
};

struct BTree *
BTree_create(int treeorder);
void
BTree_destroy(struct BTree *);
#endif //_BTREE_H_
