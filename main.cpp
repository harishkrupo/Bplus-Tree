#include<iostream>

#include "btree.h"

int main(int argc, char** argv) {
	struct BTree *tree = NULL;

	tree = BTree_create(2);
	std::cout << BTree_print(tree) << std::endl;

	for (int i = 0; i < 10; i++) {
		BTree_insert(tree, i, NULL);
		std::cout << "inserted key " << std::to_string(i) << ": " << BTree_print(tree) << std::endl;
	}

	return 0;
}
