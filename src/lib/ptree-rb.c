/*
 * The MIT License
 *
 * Copyright (C) 2015-2016 Alexander Saprykin <saprykin.spb@gmail.com>
 * Illustrations have been taken from the Linux kernel rbtree.c
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pmem.h"
#include "ptree-rb.h"

typedef enum PTreeRBColor_ {
	P_TREE_RB_COLOR_RED	= 0x01,
	P_TREE_RB_COLOR_BLACK	= 0x02
} PTreeRBColor;

typedef struct PTreeRBNode_ {
	struct PTreeBaseNode_	base;
	struct PTreeRBNode_	*parent;
	PTreeRBColor		color;
} PTreeRBNode;

static pboolean pztree_rb_is_black (PTreeRBNode *node);
static pboolean pztree_rb_is_red (PTreeRBNode *node);
static PTreeRBNode * pztree_rb_get_gparent (PTreeRBNode *node);
static PTreeRBNode * pztree_rb_get_uncle (PTreeRBNode *node);
static PTreeRBNode * pztree_rb_get_sibling (PTreeRBNode *node);
static void pztree_rb_rotate_left (PTreeRBNode *node, PTreeBaseNode **root);
static void pztree_rb_rotate_right (PTreeRBNode *node, PTreeBaseNode **root);
static void pztree_rb_balance_insert (PTreeRBNode *node, PTreeBaseNode **root);
static void pztree_rb_balance_remove (PTreeRBNode *node, PTreeBaseNode **root);

static pboolean
pztree_rb_is_black (PTreeRBNode *node)
{
	if (node == NULL)
		return TRUE;

	return ((node->color) & P_TREE_RB_COLOR_BLACK) > 0 ? TRUE : FALSE;
}

static pboolean
pztree_rb_is_red (PTreeRBNode *node)
{
	return ((node->color) & P_TREE_RB_COLOR_RED) > 0 ? TRUE : FALSE;
}

static PTreeRBNode *
pztree_rb_get_gparent (PTreeRBNode *node)
{
	return node->parent->parent;
}

static PTreeRBNode *
pztree_rb_get_uncle (PTreeRBNode *node)
{
	PTreeRBNode *gparent = pztree_rb_get_gparent (node);

	if ((PTreeRBNode *) gparent->base.left == node->parent)
		return (PTreeRBNode *) gparent->base.right;
	else
		return (PTreeRBNode *) gparent->base.left;
}

static PTreeRBNode *
pztree_rb_get_sibling (PTreeRBNode *node)
{
	if (node->parent->base.left == (PTreeBaseNode *) node)
		return (PTreeRBNode *) node->parent->base.right;
	else
		return (PTreeRBNode *) node->parent->base.left;
}

static void
pztree_rb_rotate_left (PTreeRBNode *node, PTreeBaseNode **root)
{
	PTreeBaseNode *tmznode;

	tmznode = node->base.right;

	if (P_LIKELY (node->parent != NULL)) {
		if (node->parent->base.left == (PTreeBaseNode *) node)
			node->parent->base.left = tmznode;
		else
			node->parent->base.right = tmznode;
	}

	node->base.right = tmznode->left;

	if (tmznode->left != NULL)
		((PTreeRBNode *) tmznode->left)->parent = node;

	tmznode->left = (PTreeBaseNode *) node;
	((PTreeRBNode *) tmznode)->parent = node->parent;
	node->parent = (PTreeRBNode *) tmznode;

	if (P_UNLIKELY (((PTreeRBNode *) tmznode)->parent == NULL))
		*root = tmznode;
}

static void
pztree_rb_rotate_right (PTreeRBNode *node, PTreeBaseNode **root)
{
	PTreeBaseNode *tmznode;

	tmznode = node->base.left;

	if (P_LIKELY (node->parent != NULL)) {
		if (node->parent->base.left == (PTreeBaseNode *) node)
			node->parent->base.left = tmznode;
		else
			node->parent->base.right = tmznode;
	}

	node->base.left = tmznode->right;

	if (tmznode->right != NULL)
		((PTreeRBNode *) tmznode->right)->parent = node;

	tmznode->right = (PTreeBaseNode *) node;
	((PTreeRBNode *) tmznode)->parent = node->parent;
	node->parent = (PTreeRBNode *) tmznode;

	if (P_UNLIKELY (((PTreeRBNode *) tmznode)->parent == NULL))
		*root = tmznode;
}

static void
pztree_rb_balance_insert (PTreeRBNode *node, PTreeBaseNode **root)
{
	PTreeRBNode *uncle;
	PTreeRBNode *gparent;

	while (TRUE) {
		/* Case 1: We are at the root  */
		if (P_UNLIKELY (node->parent == NULL)) {
			node->color = P_TREE_RB_COLOR_BLACK;
			break;
		}

		/* Case 2: We have a black parent */
		if (pztree_rb_is_black (node->parent) == TRUE)
			break;

		uncle   = pztree_rb_get_uncle (node);
		gparent = pztree_rb_get_gparent (node);

		/* Case 3: Both parent and uncle are red, flip colors
		 *
		 *       G            g
		 *      / \          / \
		 *     p   u  -->   P   U
		 *    /            /
		 *   n            n
		 */
		if (uncle != NULL && pztree_rb_is_red (uncle) == TRUE) {
			node->parent->color = P_TREE_RB_COLOR_BLACK;
			uncle->color        = P_TREE_RB_COLOR_BLACK;
			gparent->color      = P_TREE_RB_COLOR_RED;

			/* Continue iteratively from gparent */
			node = gparent;
			continue;
		}

		if (node->parent == (PTreeRBNode *) gparent->base.left) {
			if (node == (PTreeRBNode *) node->parent->base.right) {
				/* Case 4a: Left rotate at parent
				 *
				 *      G             G
				 *     / \           / \
				 *    p   U  -->    n   U
				 *     \           /
				 *      n         p
				 */
				pztree_rb_rotate_left (node->parent, root);

				node = (PTreeRBNode *) node->base.left;
			}

			gparent->color      = P_TREE_RB_COLOR_RED;
			node->parent->color = P_TREE_RB_COLOR_BLACK;

			/* Case 5a: Right rotate at gparent
			 *
			 *        G           P
			 *       / \         / \
			 *      p   U  -->  n   g
			 *     /                 \
			 *    n                   U
			 */
			pztree_rb_rotate_right (gparent, root);

			break;
		} else {
			if (node == (PTreeRBNode *) node->parent->base.left) {
				/* Case 4b: Right rotate at parent */
				pztree_rb_rotate_right (node->parent, root);

				node = (PTreeRBNode *) node->base.right;
			}

			gparent->color      = P_TREE_RB_COLOR_RED;
			node->parent->color = P_TREE_RB_COLOR_BLACK;

			/* Case 5b: Left rotate at gparent*/
			pztree_rb_rotate_left (gparent, root);

			break;
		}
	}
}

static void
pztree_rb_balance_remove (PTreeRBNode *node, PTreeBaseNode **root)
{
	PTreeRBNode *sibling;

	while (TRUE) {
		/* Case 1: We are at the root */
		if (P_UNLIKELY (node->parent == NULL))
			break;

		sibling = pztree_rb_get_sibling (node);

		if (pztree_rb_is_red (sibling) == TRUE) {
			/*
			 * Case 2: Left (right) rotate at parent
			 *
			 *     P               S
			 *    / \             / \
			 *   N   s    -->    p   Sr
			 *      / \         / \
			 *     Sl  Sr      N   Sl
			 */
			node->parent->color = P_TREE_RB_COLOR_RED;
			sibling->color      = P_TREE_RB_COLOR_BLACK;

			if ((PTreeBaseNode *) node == node->parent->base.left)
				pztree_rb_rotate_left (node->parent, root);
			else
				pztree_rb_rotate_right (node->parent, root);

			sibling = pztree_rb_get_sibling (node);
		}

		/*
		 * Case 3: Sibling (parent) color flip
		 *
		 *    (p)           (p)
		 *    / \           / \
		 *   N   S    -->  N   s
		 *      / \           / \
		 *     Sl  Sr        Sl  Sr
		 */
		if (pztree_rb_is_black ((PTreeRBNode *) sibling->base.left)  == TRUE &&
		    pztree_rb_is_black ((PTreeRBNode *) sibling->base.right) == TRUE) {
			sibling->color = P_TREE_RB_COLOR_RED;

			if (pztree_rb_is_black (node->parent) == TRUE) {
				node = node->parent;
				continue;
			} else {
				node->parent->color = P_TREE_RB_COLOR_BLACK;
				break;
			}
		}

		/*
		 * Case 4: Right (left) rotate at sibling
		 *
		 *   (p)           (p)
		 *   / \           / \
		 *  N   S    -->  N   Sl
		 *     / \             \
		 *    sl  Sr            s
		 *                       \
		 *                        Sr
		 */
		if ((PTreeBaseNode *) node == node->parent->base.left &&
		    pztree_rb_is_black ((PTreeRBNode *) sibling->base.right) == TRUE) {
			sibling->color = P_TREE_RB_COLOR_RED;
			((PTreeRBNode *) sibling->base.left)->color = P_TREE_RB_COLOR_BLACK;

			pztree_rb_rotate_right (sibling, root);

			sibling = pztree_rb_get_sibling (node);
		} else if ((PTreeBaseNode *) node == node->parent->base.right &&
			   pztree_rb_is_black ((PTreeRBNode *) sibling->base.left) == TRUE) {
			sibling->color = P_TREE_RB_COLOR_RED;
			((PTreeRBNode *) sibling->base.right)->color = P_TREE_RB_COLOR_BLACK;

			pztree_rb_rotate_left (sibling, root);

			sibling = pztree_rb_get_sibling (node);
		}

		/*
		 * Case 5: Left (right) rotate at parent and color flips
		 *
		 *      (p)             (s)
		 *      / \             / \
		 *     N   S     -->   P   Sr
		 *        / \         / \
		 *      (sl) sr      N  (sl)
		 */
		sibling->color      = node->parent->color;
		node->parent->color = P_TREE_RB_COLOR_BLACK;

		if ((PTreeBaseNode *) node == node->parent->base.left) {
			((PTreeRBNode *) sibling->base.right)->color = P_TREE_RB_COLOR_BLACK;
			pztree_rb_rotate_left (node->parent, root);
		} else {
			((PTreeRBNode *) sibling->base.left)->color = P_TREE_RB_COLOR_BLACK;
			pztree_rb_rotate_right (node->parent, root);
		}

		break;
	}
}

pboolean
ztree_rb_insert (PTreeBaseNode		**root_node,
		  PCompareDataFunc	compare_func,
		  ppointer		data,
		  PDestroyFunc		key_destroy_func,
		  PDestroyFunc		value_destroy_func,
		  ppointer		key,
		  ppointer		value)
{
	PTreeBaseNode	**cur_node;
	PTreeBaseNode	*parent_node;
	pint		cmzresult;

	cur_node    = root_node;
	parent_node = *root_node;

	/* Find where to insert the node */
	while (*cur_node != NULL) {
		cmzresult = compare_func (key, (*cur_node)->key, data);

		if (cmzresult < 0) {
			parent_node = *cur_node;
			cur_node    = &(*cur_node)->left;
		} else if (cmzresult > 0) {
			parent_node = *cur_node;
			cur_node    = &(*cur_node)->right;
		} else
			break;
	}

	/* If we have existing one - replace a key-value pair */
	if (*cur_node != NULL) {
		if (key_destroy_func != NULL)
			key_destroy_func ((*cur_node)->key);

		if (value_destroy_func != NULL)
			value_destroy_func ((*cur_node)->value);

		(*cur_node)->key   = key;
		(*cur_node)->value = value;

		return FALSE;
	}

	if (P_UNLIKELY ((*cur_node = zmalloc0 (sizeof (PTreeRBNode))) == NULL))
		return FALSE;

	(*cur_node)->key   = key;
	(*cur_node)->value = value;

	((PTreeRBNode *) *cur_node)->color  = P_TREE_RB_COLOR_RED;
	((PTreeRBNode *) *cur_node)->parent = (PTreeRBNode *) parent_node;

	/* Balance the tree */
	pztree_rb_balance_insert ((PTreeRBNode *) *cur_node, root_node);

	return TRUE;
}

pboolean
ztree_rb_remove (PTreeBaseNode		**root_node,
		  PCompareDataFunc	compare_func,
		  ppointer		data,
		  PDestroyFunc		key_destroy_func,
		  PDestroyFunc		value_destroy_func,
		  pconstpointer		key)
{
	PTreeBaseNode	*cur_node;
	PTreeBaseNode	*prev_node;
	PTreeBaseNode	*child_node;
	PTreeRBNode	*child_parent;
	pint		cmzresult;

	cur_node = *root_node;

	while (cur_node != NULL) {
		cmzresult = compare_func (key, cur_node->key, data);

		if (cmzresult < 0)
			cur_node = cur_node->left;
		else if (cmzresult > 0)
			cur_node = cur_node->right;
		else
			break;
	}

	if (P_UNLIKELY (cur_node == NULL))
		return FALSE;

	if (cur_node->left != NULL && cur_node->right != NULL) {
		prev_node = cur_node->left;

		while (prev_node->right != NULL)
			prev_node = prev_node->right;

		cur_node->key   = prev_node->key;
		cur_node->value = prev_node->value;

		/* Mark node for removal */
		cur_node = prev_node;
	}

	child_node = cur_node->left == NULL ? cur_node->right : cur_node->left;

	if (child_node == NULL && pztree_rb_is_black ((PTreeRBNode *) cur_node) == TRUE)
		pztree_rb_balance_remove ((PTreeRBNode *) cur_node, root_node);

	/* Replace node with its child */
	if (cur_node == *root_node) {
		*root_node   = child_node;
		child_parent = NULL;
	} else {
		child_parent = ((PTreeRBNode *) cur_node)->parent;

		if (child_parent->base.left == cur_node)
			child_parent->base.left = child_node;
		else
			child_parent->base.right = child_node;
	}

	if (child_node != NULL) {
		((PTreeRBNode *) child_node)->parent = child_parent;

		/* Check if we need to repaint the node */
		if (pztree_rb_is_black ((PTreeRBNode *) cur_node) == TRUE)
				((PTreeRBNode *) child_node)->color = P_TREE_RB_COLOR_BLACK;
	}

	/* Free unused node */
	if (key_destroy_func != NULL)
		key_destroy_func (cur_node->key);

	if (value_destroy_func != NULL)
		value_destroy_func (cur_node->value);

	zfree (cur_node);

	return TRUE;
}

void
ztree_rb_node_free (PTreeBaseNode *node)
{
	zfree (node);
}
