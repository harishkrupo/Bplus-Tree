#include "btree.h"
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <assert.h>
#include <stack>
#include <sstream>
#include <pthread.h>

#define ZALLOC1(type) calloc(1, sizeof(type))
#define ZALLOC(n, type) calloc(n, sizeof(type))

enum BTREE_NODE_TYPE {
		      BTREE_NODE_TYPE_INTERNAL,
		      BTREE_NODE_TYPE_LEAF
};

struct BTreeNode {
	enum BTREE_NODE_TYPE type;
	long *keys;
	int nkeys;
	struct BTreeNode *parent;
	pthread_mutex_t s_lock, x_lock;
	int shared_count;
};

struct BTreeLeafNode {
	struct BTreeNode node;
	void **data;
};

struct BTreeInternalNode {
	struct BTreeNode node;
	struct BTreeNode **children;
};

struct BTree {
	int treeorder;
	struct BTreeNode *root;
};

typedef int (*lock_release_test_fn) (struct BTreeNode *node, int capacity);

static void
BTreeNode_acquire_shared_lock(struct BTreeNode *node) {
	pthread_mutex_lock(&node->s_lock);
	node->shared_count += 1;
	if (node->shared_count == 1)
		pthread_mutex_lock(&node->x_lock);
	pthread_mutex_unlock(&node->s_lock);
}

static void
BTreeNode_release_shared_lock(struct BTreeNode *node) {
	pthread_mutex_lock(&node->s_lock);
	node->shared_count -= 1;
	if (node->shared_count == 0)
		pthread_mutex_unlock(&node->x_lock);
	pthread_mutex_unlock(&node->s_lock);
}

static inline void
BTreeNode_acquire_exclusive_lock(struct BTreeNode *node) {
	pthread_mutex_lock(&node->x_lock);
}

static inline void
BTreeNode_release_exclusive_lock(struct BTreeNode *node) {
	pthread_mutex_unlock(&node->x_lock);
}

/*
 * Initializes the node, returns -1 on failure
 */
static int
BTreeNode_initialize(struct BTreeNode *node, enum BTREE_NODE_TYPE type,
		     struct BTreeNode *parent, int capacity) {
	pthread_mutexattr_t mattr;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK);
	node->nkeys = 0;
	node->keys = (long *) ZALLOC(capacity, *node->keys);
	node->type = type;
	node->parent = parent;
	pthread_mutex_init(&node->s_lock, &mattr);
	pthread_mutex_init(&node->x_lock, &mattr);
	node->shared_count = 0;

	pthread_mutexattr_destroy(&mattr);
	return 0;
}

static struct BTreeLeafNode *
BTreeLeafNode_create(int capacity, struct BTreeNode *parent)
{
	struct BTreeLeafNode *new_leaf = NULL;
	struct BTreeNode *new_node = NULL;

	new_leaf = (struct BTreeLeafNode *) ZALLOC1(struct BTreeLeafNode);
	new_leaf->data = (void **) ZALLOC(capacity, *new_leaf->data);
	new_node = (struct BTreeNode *) new_leaf;
	BTreeNode_initialize(new_node, BTREE_NODE_TYPE_LEAF, parent, capacity);
	return new_leaf;
}

struct BTree *
BTree_create(int treeorder)
{
	int capacity = (2 * treeorder - 1);
	struct BTree *tree = (struct BTree *) ZALLOC1(struct BTree);
	tree->treeorder = treeorder;
	tree->root = (struct BTreeNode *) BTreeLeafNode_create(capacity, NULL);
	return tree;
}

// Returns the node which contains the key or could potentialy hold that key
static struct BTreeNode *
BTreeNode_search(struct BTreeNode *root, long key, int exclusive_lock,
		 lock_release_test_fn release_parent_lock, int treeorder) {
	struct BTreeNode *node = root;
	struct BTreeNode *parent = NULL;
	struct BTreeInternalNode *int_node = NULL;
	bool found = false;

	// See if we can just do rightmost of left subtree
	while(node->type != BTREE_NODE_TYPE_LEAF) {
		found = false;
		if (exclusive_lock) {
			BTreeNode_acquire_exclusive_lock(node);
			if (parent && release_parent_lock(node, treeorder))
				BTreeNode_release_exclusive_lock(parent);
		} else {
			BTreeNode_acquire_shared_lock(node);
			if (parent)
				BTreeNode_release_shared_lock(parent);
		}

		parent = node;
		for (int i = 0; i < node->nkeys; i++) {
			if (key <= node->keys[i]) {
				found = true;
				int_node = (struct BTreeInternalNode *) node;
				node = int_node->children[i];
			}
		}

		if (!found) {
			// We haven't found the key but we are still inside
			// the tree. Pick the last valid child next
			int_node = (struct BTreeInternalNode *) node;
			node = int_node->children[node->nkeys];
		}
	}

	return node;
}

static int
key_comparator(const void *a, const void *b)
{
	return ( *(long*)a - *(long*)b );
}

/**
 * Returns index of the key if present or -1 if it isn't present
 * Lock Needs to be taken by the caller
 */
static int
BTreeLeafNode_search(struct BTreeNode *node, long key)
{
	long *loc = NULL;
	int ret = -1;

	assert(node->type == BTREE_NODE_TYPE_LEAF);

	loc = (long *) bsearch(&key, node->keys, node->nkeys,
			       sizeof(long), key_comparator);

	if (loc)
		ret = loc - node->keys;

	return ret;
}

int
BTree_search(struct BTree *tree, long key, void **data)
{
	struct BTreeNode *root = tree->root;
	struct BTreeNode *node = NULL;
	struct BTreeLeafNode *leaf = NULL;
	int index;
	int ret = 0;

	node = BTreeNode_search(root, key, 0, NULL, 0);

	index = BTreeLeafNode_search(node, key);

	if (index == -1) {
		*data = NULL;
		ret = BTREE_RETURN_KEY_NOT_PRESENT;
	} else {
		leaf = (struct BTreeLeafNode *) node;
		*data = leaf->data[index];
		ret = BTREE_RETURN_SUCCESS;
	}

	BTreeNode_release_shared_lock(node);

	return ret;
}

/**
 * Inserts key into the node and returns a key if we need push it up
 */
static struct BTreeNode *
BTreeLeafNode_insert(struct BTreeLeafNode *leaf, long key,
		     void *data, int treeorder)
{
	struct BTreeNode *node = (struct BTreeNode *) leaf;
	struct BTreeLeafNode *new_leaf = NULL;
	struct BTreeNode *new_node = NULL;
	int capacity = (2 * treeorder - 1);
	long displaced_key;
	void *displaced_data = NULL;
	int mid = capacity / 2;
	int count = 0;
	int pos = -1;

	// Could do the following in a single loop but seems
	// more cleaner this way

	// pick the index to place the new key
	for (int i = 0; i < node->nkeys; i++) {
		if (key < node->keys[i]) {
			pos = i;
			break;
		}
	}

	// If we didn't find a place, we need to put it at last
	if (pos == -1)
		pos = node->nkeys;

	if (pos < node->nkeys) {
		displaced_key = node->keys[pos];
		displaced_data = leaf->data[pos];
		node->keys[pos] = key;
		leaf->data[pos] = data;
	}

	for (int i = pos + 1; i < node->nkeys; i++) {
		key = node->keys[i];
		data = leaf->data[i];
		node->keys[i] = displaced_key;
		leaf->data[i] = displaced_data;
		displaced_key = key;
		displaced_data = data;
	}

	// Now copy the last elements
	if (node->nkeys + 1 <= capacity) {
		node->keys[node->nkeys] = key;
		leaf->data[node->nkeys] = data;
		node->nkeys += 1;
	} else {
		new_leaf = BTreeLeafNode_create(capacity, node->parent);
		new_node = (struct BTreeNode *) new_leaf;

		// split the node into half and move the elements to the new one
		count = 0;
		for (int i = mid + 1; i < node->nkeys; i++) {
			new_node->keys[count] = node->keys[i];
			new_leaf->data[count] = leaf->data[i];
			count++;
		}
		new_node->keys[count] = key;
		new_leaf->data[count] = data;
		count++;

		new_node->nkeys = count;
		node->nkeys = mid + 1;
	}

	return new_node;
}

static struct BTreeInternalNode *
BTreeInternalNode_create(int capacity, struct BTreeNode *parent)
{
	struct BTreeInternalNode *new_internal = NULL;
	struct BTreeNode *new_node = NULL;

	new_internal =
		(struct BTreeInternalNode *) ZALLOC1(struct BTreeInternalNode);
	new_internal->children =
		(struct BTreeNode **) ZALLOC(capacity + 1, *new_internal->children);
	new_node = (struct BTreeNode *) new_internal;
	BTreeNode_initialize(new_node, BTREE_NODE_TYPE_INTERNAL,
			     parent, capacity);

	return new_internal;
}

/**
 * Inserts into node in sorted order when node->nkeys < capacity
 */

static void
BTreeInternalNode_insert_not_full(struct BTreeInternalNode *internal, long key,
				  struct BTreeNode *left,
				  struct BTreeNode *right)
{
	struct BTreeNode *node = (struct BTreeNode *) internal;
	int pos = -1;
	long replaced_key;
	struct BTreeNode *replaced_child = NULL;

	// Could be done in a single loop, but seems cleaner this way

	// We still have space to insert
	// go throught all the children pointers to find the one
	// same as left
	for (int i = 0; i <= node->nkeys; i++) {
		if (internal->children[i] == left) {
			pos = i;
			break;
		}
	}
	// pos must be +ve otherwise something is really wrong
	assert(pos > 0);

	for (int i = pos; i < (node->nkeys + 1); i++) {
		replaced_key = node->keys[i];
		node->keys[i] = key;
		key = replaced_key;

		replaced_child = internal->children[i + 1];
		internal->children[i + 1] = right;
		right = replaced_child;

		// we could have moved to a new node, so update child's parent
		internal->children[i + 1]->parent = node;
	}

	node->nkeys += 1;
}

/**
 * Insert a key into an internal node.
 */
static void
BTreeInternalNode_insert(struct BTreeInternalNode *internal, long key,
			 struct BTreeNode *left, struct BTreeNode *right,
			 int treeorder)
{
	int capacity = (2 * treeorder - 1);
	struct BTreeNode *node = (struct BTreeNode *) internal;
	struct BTreeInternalNode *new_internal = NULL;
	struct BTreeNode *new_node = NULL;
	int mid = capacity / 2;
	long key_to_move_up;
	int new_nkeys = 0;
	struct BTreeInternalNode *insertion_internal = NULL;

	// Special case when node is NULL. Possible when splitting the root,
	// whose parent is NULL.
	// In this case just create an internal node and place the node in the
	// first poisition, set right and left properly and return
	if (!node) {
		new_internal = BTreeInternalNode_create(capacity, NULL);
		new_node = (struct BTreeNode *) new_internal;

		new_node->keys[0] = key;
		new_internal->children[0] = left;
		new_internal->children[1] = right;
		new_node->nkeys = 1;
		left->parent = new_node;
		right->parent = new_node;

		return;
	}

	if (node->nkeys < capacity) {
		BTreeInternalNode_insert_not_full(internal, key, left, right);
	} else {
		// We dont have space, first split the node and then insert
		// at the right place

		// Split routine
		key_to_move_up = node->keys[mid];

		new_internal = BTreeInternalNode_create(capacity, node->parent);
		new_node = (struct BTreeNode *) new_internal;

		// copy key, copy child and update child's parent
		for (int i = mid + 1; i < node->nkeys; i++) {
			new_node->keys[new_nkeys] = node->keys[i];
			new_internal->children[new_nkeys] = internal->children[i];
			new_internal->children[new_nkeys]->parent = new_node;
			new_nkeys += 1;
		}

		// copy the last child and update its parent
		new_internal->children[new_nkeys] =
			internal->children[node->nkeys];
		new_internal->children[new_nkeys]->parent = new_node;

		// update the sizes
		new_node->nkeys = new_nkeys;
		node->nkeys = mid;


		// Key and child insertion routine
		if (key > key_to_move_up)
			insertion_internal = new_internal;
		else
			insertion_internal = internal;

		BTreeInternalNode_insert_not_full(insertion_internal,
						  key, left, right);

		// XXX/TODO Recursion! convert this to an interative algorithm
		BTreeInternalNode_insert((struct BTreeInternalNode *)node->parent,
					 key_to_move_up, node, new_node, treeorder);
	}
}

static inline int
release_parent_exclusive_lock(BTreeNode *node, int treeorder) {
	int capacity = (2 * treeorder - 1);

	if (node->nkeys < capacity)
		return 1;

	return 0;
}

/*
 * Releases all the previously acquired exclusive locks during insertion
 */
static void
BTreeNode_release_chain_of_exclusive_locks(BTreeNode *node) {

	while(node) {
		int ret = pthread_mutex_unlock(&node->x_lock);
		// A non zero return value signifies that the lock was either
		// previously unlocked or we never locked this mutex
		// This is a stopping criterion when traversing from leaf to
		// root node releasing all the acquired locks in the path
		// if (ret)
		// 	break;
		node = node->parent;
	}
}

int
BTree_insert(struct BTree *tree, long key, void *data) {
	struct BTreeNode *root = tree->root;
	struct BTreeNode *node = NULL;
	struct BTreeNode *new_node = NULL;
	int index;

	// Find potential node for insertion
	node = BTreeNode_search(root, key, 1,
				release_parent_exclusive_lock, tree->treeorder);

	// if key is already present, return error
	index = BTreeLeafNode_search(node, key);
	if (index > -1) {
		BTreeNode_release_chain_of_exclusive_locks(node);
		return BTREE_RETURN_KEY_PRESENT;
	}

	new_node = BTreeLeafNode_insert((struct BTreeLeafNode *)node, key,
					data, tree->treeorder);

	// Insertion into the leaf could split the leaf and create a new leaf
	// Insert that leaf's last key up, into the tree
	if (new_node)
		BTreeInternalNode_insert((struct BTreeInternalNode *) node->parent,
					 node->keys[node->nkeys - 1],
					 node, new_node,
					 tree->treeorder);

	// During the balancing step, a new root could have been created, which
	// is the parent to the current root that we hold. If root's parent is
	// present, update root accordingly
	if (root->parent)
		tree->root = root->parent;

	BTreeNode_release_chain_of_exclusive_locks(node);

	return BTREE_RETURN_SUCCESS;
}

std::string
BTree_print(struct BTree *tree)
{
	std::stack<BTreeNode *> node_stack;
	std::stack<long *> key_stack;
	std::stringstream ss;
	struct BTreeNode *root = tree->root;
	struct BTreeNode *node = root;
	struct BTreeInternalNode *internal = NULL;

	BTreeNode_acquire_shared_lock(root);
	node_stack.push(node);

	while(!node_stack.empty()) {
		node = node_stack.top();
		node_stack.pop();
		if (!node) {
			long *key = key_stack.top();
			key_stack.pop();

			if (!key)
				ss << ")";
			else
				ss << " " << std::to_string(*key) << " ";

			continue;
		}

		// Push NULLs to place a ')'
		node_stack.push(NULL);
		key_stack.push(NULL);

		ss << "(";

		if (node->type == BTREE_NODE_TYPE_INTERNAL) {
			for (int i = node->nkeys - 1 ; i > -1; i--) {
				internal = (struct BTreeInternalNode *) node;
				node_stack.push(internal->children[i + 1]);
				node_stack.push(NULL);

				key_stack.push(&node->keys[i]);
			}

			// Still need to push the first child
			node_stack.push(internal->children[0]);

		} else {
			std::string seperator = "";
			for (int i = 0; i < node->nkeys; i++) {
				ss << seperator << std::to_string(node->keys[i]);
				seperator = " ";
			}
		}
	}

	BTreeNode_release_shared_lock(root);
	return ss.str();
}
