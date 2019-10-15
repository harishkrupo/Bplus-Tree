#include "btree.h"
#include <iostream>
#include <cstdlib>
#include <assert.h>
#include <vector>

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

static struct BTreeLeafNode *
BTreeLeafNode_create(int capacity, struct BTreeNode *parent)
{
	struct BTreeLeafNode *new_leaf = NULL;
	struct BTreeNode *new_node = NULL;

	new_leaf = (struct BTreeLeafNode *) ZALLOC1(struct BTreeLeafNode);
	new_leaf->data = (void **) ZALLOC(capacity, *new_leaf->data);
	new_node = (struct BTreeNode *) new_leaf;
	new_node->nkeys = 0;
	new_node->keys = (long *) ZALLOC(capacity, *new_node->keys);
	new_node->type = BTREE_NODE_TYPE_LEAF;
	new_node->parent = parent;

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
BTreeNode_search(struct BTreeNode *root, long key) {
	struct BTreeNode *node = root;
	struct BTreeInternalNode *int_node = NULL;
	bool found = false;

	// See if we can just do rightmost of left subtree
	while(node->type != BTREE_NODE_TYPE_LEAF) {
		found = false;
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

void *
BTree_search(struct BTree *tree, long key)
{
	struct BTreeNode *root = tree->root;
	struct BTreeNode *node = NULL;
	struct BTreeLeafNode *leaf = NULL;
	void *data = NULL;
	int index;

	node = BTreeNode_search(root, key);
	index = BTreeLeafNode_search(node, key);

	if (index == -1) {
		data = NULL;
	} else {
		leaf = (struct BTreeLeafNode *) node;
		data = leaf->data[index];
	}

	return data;
}

/**
 * Inserts key into the node and returns a key if we need push it up
 */
static struct BTreeNode *
BTreeLeafNode_insert(struct BTreeNode *node, long key,
		     void *data, int treeorder)
{
	struct BTreeLeafNode *leaf = (struct BTreeLeafNode *) node;
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
		node->nkeys = mid;
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
	new_node->nkeys = 0;
	new_node->keys = (long *) ZALLOC(capacity, *new_node->keys);
	new_node->type = BTREE_NODE_TYPE_INTERNAL;
	new_node->parent = parent;

	return new_internal;
}

/**
 * Insert a key into an internal node
 * returns a new node if one was created
 */
static struct BTreeNode *
BTreeInternalNode_insert(struct BTreeNode *node, long key,
			 struct BTreeNode *left, struct BTreeNode *right,
			 int treeorder)
{
	struct BTreeNode *new_node = NULL;
	struct BTreeInternalNode *internal = (struct BTreeInternalNode *) node;
	struct BTreeInternalNode *new_internal = NULL;
	int capacity = (2 * treeorder - 1);
	long displaced_key;
	struct BTreeNode *displaced_child = NULL;
	int mid = capacity / 2;
	int count = 0;
	int pos = -1;

	// go throught all the children pointers to find the one same as left
	for (int i = 0; i <= node->nkeys; i++) {
		if (internal->children[i] == left) {
			pos = i;
			break;
		}
	}

	// if pos is -ve, something is really wrong
	assert(pos > 0);

	// Place the key at pos and remember that key and its right child
	displaced_key = node->keys[pos];
	displaced_child = internal->children[pos + 1];
	node->keys[pos] = key;
	internal->children[pos + 1] = right;

	// Now start moving the displaced node one by one
	for (int i = pos + 1; i < node->nkeys; i++) {
		key = node->keys[i];
		right = internal->children[i + 1];
		node->keys[i] = displaced_key;
		internal->children[i + 1] = displaced_child;
		displaced_key = key;
		displaced_child = right;
	}

	// need to write one more, check capacity before writing
	if (node->nkeys + 1 <= capacity) {
		node->keys[node->nkeys] = key;
		internal->children[node->nkeys + 1] = right;
		node->nkeys += 1;
	} else {
		new_internal = BTreeInternalNode_create(capacity, node->parent);
		new_node = (struct BTreeNode *) new_internal;

		count = 0;
		for (int i = mid + 1; i < node->nkeys; i++) {
			new_node->keys[count] = node->keys[i];
			new_internal->children[count] = internal->children[i];
			count++;
		}

		// key, right were meant to be written last after displacement
		new_node->keys[count] = key;
		new_internal->children[count] = right;
		count++;

		new_node->nkeys = count;
		node->nkeys = mid;
	}

	return new_node;
}


int
BTree_insert(struct BTree *tree, long key, void *data) {
	struct BTreeNode *root = tree->root;
	struct BTreeNode *node = NULL;
	struct BTreeNode *new_node = NULL;
	int index;

	// Find potential node for insertion
	node = BTreeNode_search(root, key);

	// if key is already present, return error
	index = BTreeLeafNode_search(node, key);
	if (index > -1)
		return BTREE_RETURN_KEY_PRESENT;

	new_node = BTreeLeafNode_insert(node, key, data, tree->treeorder);

	// We have just added a new node, need to balance the tree
	// till the tree is fully balanced
	// node's (the old node) right most key is the key to be inserted
	while(new_node) {
		new_node = BTreeInternalNode_insert(node->parent,
						    node->keys[node->nkeys - 1],
						    node, new_node,
						    tree->treeorder);
	}

	// During the balancing step, a new root could have been created, which
	// is the parent to the current root that we hold, update root
	// accordingly
	tree->root = root->parent;

	return BTREE_RETURN_SUCCESS;
}
