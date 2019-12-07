#include<iostream>
#include<fstream>
#include<sstream>
#include<cstdio>
#include<pthread.h>

#include "btree.h"

#define MAXFILES 100

#define TLOG(format, ...)				\
	printf("Thread %lu: " format, id, __VA_ARGS__)

std::ifstream fs[MAXFILES];
pthread_t threads[MAXFILES];
struct BTree *tree = NULL;

void *run_thread(void *_id) {
	std::string str;
	uint64_t id = (uint64_t) _id;
	uint64_t operation, key;
	void *data = NULL;
	int ret = 0;
	while(std::getline(fs[id], str)) {
		std::stringstream ss(str);
		ss >> operation;
		ss >> key;
		TLOG("Operation %lu key %lu\n", operation, key);
		switch (operation) {
		case 1: // SEARCH
			ret = BTree_search(tree, key, &data);
			if (ret == BTREE_RETURN_KEY_NOT_PRESENT)
				TLOG("Searched key %lu unavailable\n", key);
			else
				TLOG("Searched for key %lu data %p\n", key, data);
			break;
		case 2: // Insert
			ret = BTree_insert(tree, key, NULL);
			if (ret == BTREE_RETURN_KEY_PRESENT)
				TLOG("Trying to insert existing key %lu\n", key);
			else
				TLOG("Inserted data %p for key %lu\n", data, key);
			break;
		}
	}

	return NULL;
}

int main(int argc, char** argv) {
	int n = 10;

	std::ios::sync_with_stdio(true);

	tree = BTree_create(2);
	std::cout << "Creating an empty tree... ";
	std::cout << BTree_print(tree) << std::endl;

	printf("Inserting %d elements\n", n);
	for (int i = 0; i < n; i++) {
		BTree_insert(tree, i, NULL);
		std::cout << "inserted key " << std::to_string(i) << ": " << BTree_print(tree) << std::endl;
	}

	printf("number of files: %d\n", argc - 1);
	if (argc - 1 > MAXFILES) {
		printf("Cannot process more than %d files\n", MAXFILES);
		exit(EXIT_SUCCESS);
	}

	for (int i = 1; i < argc; i++) {
		uint64_t id = i - 1;
		fs[id].open(argv[i]);
		pthread_create(&threads[id], NULL, run_thread, (void *) id);
	}

	for (int i = 1; i < argc; i++) {
		uint64_t id = i - 1;
		pthread_join(threads[id], NULL);
		fs[id].close();
	}

	return 0;
}
