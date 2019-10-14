#include<iostream>

#include "btree.h"

int main(int argc, char** argv) {
	struct BTree *tree = NULL;

	tree = BTree_create(2);
	for (int i = 0; i < 10; i++) {
		BTree_insert(tree, i, NULL);
	}

	return 0;
}
