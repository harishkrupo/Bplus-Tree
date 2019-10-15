#ifndef _BTREE_H_
#define _BTREE_H_

#include <iostream>

struct BTree;

enum BTREE_RETURN {
		  BTREE_RETURN_SUCCESS = 0,
		  BTREE_RETURN_KEY_PRESENT
};

struct BTree *
BTree_create(int treeorder);
void
BTree_destroy(struct BTree *);
void *
BTree_search(struct BTree *tree, long key);
int
BTree_insert(struct BTree *tree, long key, void *data);
std::string
BTree_print(struct BTree *tree);
#endif //_BTREE_H_
