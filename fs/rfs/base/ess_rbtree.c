/**
 *     @mainpage   Nestle Layer
 *
 *     @section Intro
 *       TFS5 File system's VFS framework
 *   
 *     @MULTI_BEGIN@ @COPYRIGHT_DEFAULT
 *     @section Copyright COPYRIGHT_DEFAULT
 *            COPYRIGHT. SAMSUNG ELECTRONICS CO., LTD.
 *                                    ALL RIGHTS RESERVED
 *     Permission is hereby granted to licensees of Samsung Electronics Co., Ltd. products
 *     to use this computer program only in accordance 
 *     with the terms of the SAMSUNG FLASH MEMORY DRIVER SOFTWARE LICENSE AGREEMENT.
 *     @MULTI_END@
 *
 */

/** 
 * @file		ess_rbtree.c
 * @brief		The file implements for ESS_RBTree module 
 * @author		Zhang Qing(qing1.zhang@samsung.com)
 * @version		JUL-26-2006 [Zhang Qing] First writing
 * @see			None
 */

// debug begin
#include "ess_debug.h"
// debug end

// include for ESS 
#include "ess_types.h"
#include "ess_rbtree.h"


// enum
enum _RBNodeColor
{
	_RED	= 0,
	_BLACK	= 1
};

// define
#define _KEY			ESS_RBTREE_NODE_KEY
#define _COLOR			ESS_RBTREE_NODE_COLOR
#define _LEFT			ESS_RBTREE_NODE_LEFT
#define _RIGHT			ESS_RBTREE_NODE_RIGHT
#define _PARENT			ESS_RBTREE_NODE_PARENT

#define _GRAND_PARENT(pRBNode)	(_PARENT(_PARENT(pRBNode)))

#define _HEADER			ESS_RBTREE_HEADER
#define _ROOT			ESS_RBTREE_ROOT
#define _LEFT_MOST		ESS_RBTREE_LEFT_MOST
#define _RIGHT_MOST		ESS_RBTREE_RIGHT_MOST

/** 
 * _RBT_NodeIsLeftChild judges whether a node is left node or not
 * 
 * @param pRBNode 	: [in] A node pointer
 * 
 * @return FFAT_TRUE	: Node is left node
 * @return FFAT_FALSE	: Node is not left node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static t_boolean
_RBT_NodeIsLeftChild(EssRBNode* pRBNode)
{
	ESS_ASSERT(pRBNode);

	return ((pRBNode == _LEFT(_PARENT(pRBNode))) ?
			ESS_TRUE : ESS_FALSE);
}

/** 
 * _RBT_NodeIsRightChild judges whether a node is right node or not
 * 
 * @param pRBNode 	: [in] A node pointer
 * 
 * @return FFAT_TRUE	: Node is right node
 * @return FFAT_FALSE	: Node is not right node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static t_boolean
_RBT_NodeIsRightChild(EssRBNode* pRBNode)
{
	ESS_ASSERT(pRBNode);

	return ((pRBNode == _RIGHT(_PARENT(pRBNode))) ?
			ESS_TRUE : ESS_FALSE);
}

/** 
 * _RBT_NodeParentIsLeftChild judges whether a node's parent is left node or not
 * 
 * @param pRBNode 	: [in] A node pointer
 * 
 * @return FFAT_TRUE	: Node is left node
 * @return FFAT_FALSE	: Node is not left node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static t_boolean
_RBT_NodeParentIsLeftChild(EssRBNode* pRBNode)
{
	ESS_ASSERT(pRBNode);

	return (_RBT_NodeIsLeftChild(_PARENT(pRBNode)));
}

/**
* Left rotate a rbtree on node pRBNode
*
* @param		pRBTree		: [in] The header node of a rbtree
* @param		pRBNode		: [in] Node be left rotated
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
static void
_RBT_LeftRotate(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	EssRBNode* pOldRight;

	ESS_ASSERT(pRBTree && pRBNode);

	pOldRight = _RIGHT(pRBNode);
	_RIGHT(pRBNode) = _LEFT(pOldRight);
	if (_LEFT(pOldRight) != NULL)
	{
		_PARENT(_LEFT(pOldRight)) = pRBNode;
	}
	_PARENT(pOldRight) = _PARENT(pRBNode);

	if (pRBNode == _ROOT(pRBTree))
	{
		_ROOT(pRBTree) = pOldRight;
	}
	else if (_RBT_NodeIsLeftChild(pRBNode))
	{
		_LEFT(_PARENT(pRBNode)) = pOldRight;
	} 
	else
	{
		_RIGHT(_PARENT(pRBNode)) = pOldRight;
	}
	_LEFT(pOldRight) = pRBNode;
	_PARENT(pRBNode) = pOldRight;
}

/**
* Right rotate a rbtree on node pRBNode
*
* @param		pRBTree		: [in] The header node of a rbtree
* @param		pRBNode		: [in] Node be right rotated
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
static void
_RBT_RightRotate(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	EssRBNode* pOldLeft;

	ESS_ASSERT(pRBTree && pRBNode);

	pOldLeft = _LEFT(pRBNode);
	_LEFT(pRBNode) = _RIGHT(pOldLeft);
	if (_RIGHT(pOldLeft) != NULL)
	{
		_PARENT(_RIGHT(pOldLeft)) = pRBNode;
	}
	_PARENT(pOldLeft) = _PARENT(pRBNode);

	if (pRBNode == _ROOT(pRBTree))
	{
		_ROOT(pRBTree) = pOldLeft;
	}
	else if (_RBT_NodeIsRightChild(pRBNode))
	{
		_RIGHT(_PARENT(pRBNode)) = pOldLeft;
	} 
	else
	{
		_LEFT(_PARENT(pRBNode)) = pOldLeft;
	}
	_RIGHT(pOldLeft) = pRBNode;
	_PARENT(pRBNode) = pOldLeft;
}

/**
* Initialize a rbtree
*
* @param		pRBTree		: [in] The header node of a rbtree, which represents the rbtree
* @return		ESS_OK		: Init success
* @return		ESS_EINVALID: Invalid rbtree
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
t_int32 
EssRBTree_Init(EssRBTree* pRBTree)
{
	EssRBNode* pHeader;

#ifdef ESS_RBT_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree);

	pHeader = _HEADER(pRBTree);

	_COLOR(pHeader)	= _RED;
	_PARENT(pHeader) = NULL;
	_LEFT(pHeader)	= NULL;
	_RIGHT(pHeader)	= NULL;

	_LEFT_MOST(pRBTree)	= pHeader;
	_RIGHT_MOST(pRBTree)	= pHeader;

	return ESS_OK;
}

/** 
 * _RBT_DelNodeWithSucReplace replaces pDelNode with pSucNode. It looks like original pSucNode is deleted.
 * 
 * @param pRBTree 				: [in] A RBTree
 * @param pNodeToDelete 		: [in] Node to be deleted
 * @param pSucNode 				: [in] Successor node of node to be deleted
 * @param ppNodeToFix 			: [out] Node to be fixed
 * @param ppNodeToFixParent 	: [out] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_DelNodeWithSucReplace(EssRBTree* pRBTree,
							EssRBNode* pDelNode,
							EssRBNode* pSucNode,
							EssRBNode** ppNodeToFix,
							EssRBNode** ppNodeToFixParent)
{
	EssRBNode* pSucNodeChild;
	t_boolean bSucIsDelNodeChild;
	t_uint32 dwColorTmp;

	ESS_ASSERT(pRBTree && pDelNode && pSucNode);

	if (_RIGHT(pDelNode) == pSucNode) 
	{
		bSucIsDelNodeChild = ESS_TRUE;
	}
	else
	{
		bSucIsDelNodeChild = ESS_FALSE;
	}

	pSucNodeChild = _RIGHT(pSucNode); // pSucNodeChild might be null

	//Get *ppNodeToFix and *ppNodeToFixParent
	*ppNodeToFix = pSucNodeChild;
	if (!bSucIsDelNodeChild)
	{
		*ppNodeToFixParent = _PARENT(pSucNode);
	}
	else
	{
		*ppNodeToFixParent = pSucNode;
	}

	//1. Disconnect pSucNode with its right child 
	//   (pSucNode has not left child)
	//   Connect _RIGHT(pSucNode) with _PARENT(pSucNode)
	if (!bSucIsDelNodeChild) 
	{
		if (pSucNodeChild != NULL) 
		{
			_PARENT(pSucNodeChild) = _PARENT(pSucNode);
		}
		_LEFT(_PARENT(pSucNode)) = pSucNodeChild;
	}

	//2. Connect _LEFT(pDelNode) with pSucNode
	_PARENT(_LEFT(pDelNode)) = pSucNode;
	_LEFT(pSucNode) = _LEFT(pDelNode);

	//3. Connect _RIGHT(pDelNode) with pSucNode
	if (!bSucIsDelNodeChild) 
	{
		_RIGHT(pSucNode) = _RIGHT(pDelNode);
		_PARENT(_RIGHT(pDelNode)) = pSucNode;
	}

	//4. Connect _PARENT(pDelNode) with pSucNode
	if (_ROOT(pRBTree) == pDelNode)
	{
		_ROOT(pRBTree) = pSucNode;
	}
	else if (_RBT_NodeIsLeftChild(pDelNode))
	{
		_LEFT(_PARENT(pDelNode)) = pSucNode;
	}
	else //pDelNode is right child
	{
		_RIGHT(_PARENT(pDelNode)) = pSucNode;
	}
	_PARENT(pSucNode) = _PARENT(pDelNode);

	//Exchange pSucNode color with pDelNode color 
	dwColorTmp = _COLOR(pSucNode);
	_COLOR(pSucNode) = _COLOR(pDelNode);
	_COLOR(pDelNode) = dwColorTmp;

}

/** 
 * _RBT_DelNodeDirectly deletes a node from RBTree directly
 * 
 * @param pRBTree 				: [in] A RBTree
 * @param pNodeToDelete 		: [in] Node to be deleted
 * @param ppNodeToFix 			: [out] Node to be fixed
 * @param ppNodeToFixParent 	: [out] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_DelNodeDirectly(EssRBTree* pRBTree, 
					 EssRBNode* pDelNode,
					 EssRBNode** ppNodeToFix,
					 EssRBNode** ppNodeToFixParent)
{
	EssRBNode* pDelNodeChild;
	EssRBNode* pNodeTmp;

	ESS_ASSERT(pRBTree && pDelNode);

	if (_LEFT(pDelNode) == NULL)
	{
		pDelNodeChild = _RIGHT(pDelNode); // pDelNodeChild might be null
	} 
	else
	{
		pDelNodeChild = _LEFT(pDelNode); // pDelNodeChild is not null
	} 

	//Get pNodeToFix and pNodeToFixParent
	*ppNodeToFix = pDelNodeChild;
	*ppNodeToFixParent = _PARENT(pDelNode);

	if (_ROOT(pRBTree) == pDelNode)
	{
		_ROOT(pRBTree) = pDelNodeChild;
	}
	else 
	{
		//Connect _PARENT(pDelNode) with pDelNodeChild
		if (_RBT_NodeIsLeftChild(pDelNode))
		{
			_LEFT(_PARENT(pDelNode)) = pDelNodeChild;
		}
		else //pDelNode is right child
		{
			_RIGHT(_PARENT(pDelNode)) = pDelNodeChild;
		}
	}

	if (pDelNode == _LEFT_MOST(pRBTree))
	{
		if (_RIGHT(pDelNode) == NULL)
		{
			_LEFT_MOST(pRBTree) = _PARENT(pDelNode);
		}
		else
		{
			pNodeTmp = pDelNode;
			while (_LEFT(pNodeTmp) != NULL) 
				pNodeTmp = _LEFT(pNodeTmp);
			_LEFT_MOST(pRBTree) = pNodeTmp;
		}
	}

	if (pDelNode == _RIGHT_MOST(pRBTree))
	{
		if (_LEFT(pDelNode) == NULL)
		{
			_RIGHT_MOST(pRBTree) = _PARENT(pDelNode);
		}
		else
		{
			pNodeTmp = pDelNode;
			while (_RIGHT(pNodeTmp) != NULL) 
				pNodeTmp = _RIGHT(pNodeTmp);
			_RIGHT_MOST(pRBTree) = pNodeTmp;
		}
	}

}

/** 
 * _RBT_Delete deletes a node from RBTree
 * 
 * @param pRBTree 				: [in] A RBTree
 * @param pNodeToDelete 		: [in] Node to be deleted
 * @param ppNodeToFix 			: [out] Node to be fixed
 * @param ppNodeToFixParent 	: [out] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_Delete(EssRBTree* pRBTree, 
			EssRBNode* pNodeToDelete, 
			EssRBNode** ppNodeToFix,
			EssRBNode** ppNodeToFixParent)
{
	EssRBNode* pSuccessor = NULL;
	t_boolean bDelNodeHas2Child;

	ESS_ASSERT(pRBTree && pNodeToDelete);

	if ((_LEFT(pNodeToDelete) == NULL) || (_RIGHT(pNodeToDelete) == NULL))
	{
		bDelNodeHas2Child = ESS_FALSE;
	} 
	else
	{
		bDelNodeHas2Child = ESS_TRUE;

		pSuccessor = _RIGHT(pNodeToDelete);
		while (_LEFT(pSuccessor) != NULL)
		{
			pSuccessor = _LEFT(pSuccessor);
		}
		//pSuccessor has not left child
	}

	if (bDelNodeHas2Child)
	{
		_RBT_DelNodeWithSucReplace(pRBTree, pNodeToDelete, pSuccessor, 
			ppNodeToFix, ppNodeToFixParent);
	}
	else
	{
		_RBT_DelNodeDirectly(pRBTree, pNodeToDelete, 
			ppNodeToFix, ppNodeToFixParent);
	}

	//*ppNodeToFix might be null, but *ppNodeToFixParent is not null
	ESS_ASSERT(*ppNodeToFixParent);
}

/** 
 * _RBT_ReBalanceForDelete re-balance a rbtree after deleting a node
 * 
 * @param pRBTree 			: [in] A RBTree
 * @param pNodeToFix 		: [in] Node to be fixed
 * @param pNodeToFixParent 	: [in] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_ReBalanceForDelete(EssRBTree* pRBTree, 
						EssRBNode* pNodeToFix,
						EssRBNode* pNodeToFixParent)
{
	EssRBNode* pSiblingNode;

	ESS_ASSERT(pRBTree && pNodeToFixParent);

	//pNodeToFix can be viewed as having an "extra" black
	//The objective is to move the "extra" black up until
	//(1) pNodeToFix points to a red node, color pNodeToFix black
	//(2) pNodeToFix points to root, then "extra" black can be simply removed

	while ( (pNodeToFix != _ROOT(pRBTree))
		&& ( (pNodeToFix == NULL) || (_COLOR(pNodeToFix) == _BLACK) ) )
	{
		if (pNodeToFix == _LEFT(pNodeToFixParent))
		{
			pSiblingNode = _RIGHT(pNodeToFixParent); //pSiblingNode must not be null

			if (_COLOR(pSiblingNode) == _RED)
			{
				_COLOR(pSiblingNode) = _BLACK;
				_COLOR(pNodeToFixParent) = _RED;
				_RBT_LeftRotate(pRBTree, pNodeToFixParent);
				pSiblingNode = _RIGHT(pNodeToFixParent);
			}

			if ( ( (_LEFT(pSiblingNode) == NULL) || (_COLOR(_LEFT(pSiblingNode)) == _BLACK) )
				&& ( (_RIGHT(pSiblingNode) == NULL) || (_COLOR(_RIGHT(pSiblingNode)) == _BLACK) ) )
			{
				_COLOR(pSiblingNode) = _RED;
				pNodeToFix = pNodeToFixParent;
				pNodeToFixParent = _PARENT(pNodeToFix);
			} 
			else
			{
				if ( (_RIGHT(pSiblingNode) == NULL) || (_COLOR(_RIGHT(pSiblingNode)) == _BLACK) )
				{
					if (_LEFT(pSiblingNode) != NULL)
					{
						_COLOR(_LEFT(pSiblingNode)) = _BLACK;
					}
					_COLOR(pSiblingNode) = _RED;
					_RBT_RightRotate(pRBTree, pSiblingNode);
					pSiblingNode = _RIGHT(pNodeToFixParent);
				}

				_COLOR(pSiblingNode) = _COLOR(pNodeToFixParent);
				_COLOR(pNodeToFixParent) = _BLACK;
				if (_RIGHT(pSiblingNode) != NULL)
				{
					_COLOR(_RIGHT(pSiblingNode)) = _BLACK;
				}
				_RBT_LeftRotate(pRBTree, pNodeToFixParent);
				break;
			}
		}
		else
		{
			pSiblingNode = _LEFT(pNodeToFixParent); //pSiblingNode must not be null

			if (_COLOR(pSiblingNode) == _RED)
			{
				_COLOR(pSiblingNode) = _BLACK;
				_COLOR(pNodeToFixParent) = _RED;
				_RBT_RightRotate(pRBTree, pNodeToFixParent);
				pSiblingNode = _LEFT(pNodeToFixParent);
			}

			if ( ( (_RIGHT(pSiblingNode) == NULL) || (_COLOR(_RIGHT(pSiblingNode)) == _BLACK) )
				&& ( (_LEFT(pSiblingNode) == NULL) || (_COLOR(_LEFT(pSiblingNode)) == _BLACK) ) )
			{
				_COLOR(pSiblingNode) = _RED;
				pNodeToFix = pNodeToFixParent;
				pNodeToFixParent = _PARENT(pNodeToFix);
			} 
			else
			{
				if ( (_LEFT(pSiblingNode) == NULL) || (_COLOR(_LEFT(pSiblingNode)) == _BLACK) )
				{
					if (_RIGHT(pSiblingNode) != NULL)
					{
						_COLOR(_RIGHT(pSiblingNode)) = _BLACK;
					}
					_COLOR(pSiblingNode) = _RED;
					_RBT_LeftRotate(pRBTree, pSiblingNode);
					pSiblingNode = _LEFT(pNodeToFixParent);
				}

				_COLOR(pSiblingNode) = _COLOR(pNodeToFixParent);
				_COLOR(pNodeToFixParent) = _BLACK;
				if (_LEFT(pSiblingNode) != NULL)
				{
					_COLOR(_LEFT(pSiblingNode)) = _BLACK;
				}
				_RBT_RightRotate(pRBTree, pNodeToFixParent);
				break;
			}
		}
	}

	// pNodeToFix points to a red node, color pNodeToFix black
	if (pNodeToFix)
	{
		_COLOR(pNodeToFix) = _BLACK;
	}
}

/**
 * Delete a node from rbtree.
 * If delete success (return value is ESS_OK), the caller of EssRBTree_Delete should free pRBNode by itself.
 * It cannot be freed at here.
 * @param		pRBTree		: [in] A rbtree 
 * @param		pRBNode		: [in] Node to be deleted (key must be assigned to pRBNode)
 * @return		ESS_OK		: Delete success
 * @return		ESS_EINVALID: Invalid rbtree or node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
t_int32 
EssRBTree_Delete(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	EssRBNode* pNodeToFix;
	EssRBNode* pNodeToFixParent;

#ifdef ESS_RBT_STRICT_CHECK
	if ((pRBTree == NULL) || (pRBNode == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree && pRBNode);

	pNodeToFix = NULL;
	pNodeToFixParent = NULL;

	_RBT_Delete(pRBTree, pRBNode, &pNodeToFix, &pNodeToFixParent);
	if (_COLOR(pRBNode) == _BLACK)
	{
		_RBT_ReBalanceForDelete(pRBTree, pNodeToFix, pNodeToFixParent);
	}

	return ESS_OK;
}

/** 
 * _RBT_FindInsertPoint finds the insert point to insert a node into RBTree
 * 
 * @param pRBTree 			: A RBTree
 * @param pNodeToInsert 	: Node to be inserted
 * 
 * @return Insert point
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static EssRBNode* 
_RBT_FindInsertPoint(EssRBTree* pRBTree, 
					 EssRBNode* pNodeToInsert)
{
	EssRBNode* pInsertPoint;
	EssRBNode* pNodeTmp;
	t_uint32 dwKey;

	ESS_ASSERT(pRBTree && pNodeToInsert);

	dwKey = _KEY(pNodeToInsert);
	pInsertPoint = _HEADER(pRBTree);
	pNodeTmp = _ROOT(pRBTree);

	while (pNodeTmp != NULL) 
	{
		pInsertPoint = pNodeTmp;
		pNodeTmp = (dwKey < _KEY(pNodeTmp)) ? _LEFT(pNodeTmp) : _RIGHT(pNodeTmp);
	}

	return pInsertPoint;
}

/** 
 * _RBT_Insert inserts a node into RBTree
 * 
 * @param pRBTree 			: A RBTree
 * @param pNodeToInsert 	: Node to be inserted
 * @param pInsertPoint 		: Insert point
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_Insert(EssRBTree* pRBTree, 
			EssRBNode* pNodeToInsert,
			EssRBNode* pInsertPoint)
{
	EssRBNode* pHeader;

	ESS_ASSERT(pRBTree && pNodeToInsert && pInsertPoint);

	pHeader = _HEADER(pRBTree);
	if (pInsertPoint == pHeader)
	{
		_LEFT(pHeader) = pNodeToInsert;
		_ROOT(pRBTree) = pNodeToInsert;
		_RIGHT_MOST(pRBTree) = pNodeToInsert;
	}
	else if (_KEY(pNodeToInsert) < _KEY(pInsertPoint))
	{
		_LEFT(pInsertPoint) = pNodeToInsert;
		if (pInsertPoint == _LEFT_MOST(pRBTree))
		{
			_LEFT_MOST(pRBTree) = pNodeToInsert;
		}
	} 
	else
	{
		_RIGHT(pInsertPoint) = pNodeToInsert;
		if (pInsertPoint == _RIGHT_MOST(pRBTree))
		{
			_RIGHT_MOST(pRBTree) = pNodeToInsert;
		}
	}

	_PARENT(pNodeToInsert) = pInsertPoint;
	_LEFT(pNodeToInsert)	= NULL;
	_RIGHT(pNodeToInsert)	= NULL;
}

/** 
 * _RBT_ReBalance rebalances a RBTree after insert a node
 * 
 * @param pRBTree 	: A RBTree
 * @param pRBNode 	: Node be inserted
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_ReBalance(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	EssRBNode* pUncle;

	ESS_ASSERT(pRBTree && pRBNode);

	_COLOR(pRBNode) = _RED;

	while ((pRBNode != _ROOT(pRBTree)) 
		&& (_COLOR(_PARENT(pRBNode)) == _RED))
	{
		if (_RBT_NodeParentIsLeftChild(pRBNode))
		{
			pUncle = _RIGHT(_GRAND_PARENT(pRBNode));
			if ((pUncle) && (_COLOR(pUncle) == _RED))
			{
				_COLOR(_PARENT(pRBNode))		= _BLACK;
				_COLOR(pUncle)					= _BLACK;
				_COLOR(_GRAND_PARENT(pRBNode))	= _RED;
				pRBNode = _GRAND_PARENT(pRBNode);
			}
			else
			{
				if (_RBT_NodeIsRightChild(pRBNode))
				{
					pRBNode = _PARENT(pRBNode);
					_RBT_LeftRotate(pRBTree, pRBNode);
				}
				_COLOR(_PARENT(pRBNode)) = _BLACK;
				_COLOR(_GRAND_PARENT(pRBNode)) = _RED;
				_RBT_RightRotate(pRBTree, _GRAND_PARENT(pRBNode));
			}
		}
		else
		{
			pUncle = _LEFT(_GRAND_PARENT(pRBNode));
			if ((pUncle) && (_COLOR(pUncle) == _RED))
			{
				_COLOR(_PARENT(pRBNode))		= _BLACK;
				_COLOR(pUncle)					= _BLACK;
				_COLOR(_GRAND_PARENT(pRBNode))	= _RED;
				pRBNode = _GRAND_PARENT(pRBNode);
			}
			else
			{
				if (_RBT_NodeIsLeftChild(pRBNode))
				{
					pRBNode = _PARENT(pRBNode);
					_RBT_RightRotate(pRBTree, pRBNode);
				}
				_COLOR(_PARENT(pRBNode)) = _BLACK;
				_COLOR(_GRAND_PARENT(pRBNode)) = _RED;
				_RBT_LeftRotate(pRBTree, _GRAND_PARENT(pRBNode));
			}
		}
	}

	_COLOR(_ROOT(pRBTree)) = _BLACK;
}


/**
* Insert a node to rbtree
*
* @param		pRBTree		: A rbtree 
* @param		pRBNode		: Node to be inserted (key must be assigned to pRBNode)
* @return		ESS_OK		: Insert success
* @return		ESS_EINVALID: Invalid rbtree or node
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
t_int32 
EssRBTree_Insert(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	EssRBNode* pInsertPoint;

#ifdef ESS_RBT_STRICT_CHECK
	if ((pRBTree == NULL) || (pRBNode == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree && pRBNode);

	pInsertPoint = _RBT_FindInsertPoint(pRBTree, pRBNode);
	_RBT_Insert(pRBTree, pRBNode, pInsertPoint);
	_RBT_ReBalance(pRBTree, pRBNode);

	return ESS_OK;
}

/**
* Lookup a node which has given key from rbtree
*
* @param		pRBTree		: [in] A rbtree 
* @param		dwRBNodeKey	: [in] The key to be looked up 
* @return		pRBNode		: The node which has given key. 
* @return		NULL		: if there does not exist the node
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
EssRBNode*
EssRBTree_Lookup(EssRBTree* pRBTree, t_uint32 dwRBNodeKey)
{
	EssRBNode* pNodeTmp;
	EssRBNode* pPossibleNode;

#ifdef ESS_RBT_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return NULL;
	}
#endif

	ESS_ASSERT(pRBTree);

	pPossibleNode = NULL;
	pNodeTmp = _ROOT(pRBTree);

	while (pNodeTmp != NULL)
	{
		if (dwRBNodeKey <= _KEY(pNodeTmp))
		{
			pPossibleNode = pNodeTmp;
			pNodeTmp = _LEFT(pNodeTmp);
		} 
		else
		{
			pNodeTmp = _RIGHT(pNodeTmp);
		}
	}

	if  ((pPossibleNode == NULL) || (_KEY(pPossibleNode) != dwRBNodeKey))
	{
		return NULL;
	}

	return pPossibleNode;
}

EssRBNode*	
EssRBTree_Lookup_WithCmp(EssRBTree* pRBTree, void* pTarget, PFN_RBNODE_CMP pfCmp)
{
	EssRBNode* pNodeTmp;
	EssRBNode* pPossibleNode;

#ifdef ESS_RBT_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return NULL;
	}
#endif

	ESS_ASSERT(pRBTree);

	pPossibleNode = NULL;
	pNodeTmp = _ROOT(pRBTree);

	while (pNodeTmp != NULL)
	{
		if (pfCmp(pTarget, pNodeTmp) <= 0)
		{
			pPossibleNode = pNodeTmp;
			pNodeTmp = _LEFT(pNodeTmp);
		} 
		else
		{
			pNodeTmp = _RIGHT(pNodeTmp);
		}
	}

	if  ((pPossibleNode == NULL) || (pfCmp(pTarget, pPossibleNode) != 0))
	{
		return NULL;
	}

	return pPossibleNode;
}

// debug begin
/** 
 * _RBT_PrintNodeInfo prints a node information of RBTree
 * 
 * @param pRBTree 	: A RBTree
 * @param pRBNode 	: A RBNode
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_PrintNodeInfo(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	ESS_ASSERT(pRBTree && pRBNode);

	ESS_DEBUG_PRINTF("*********Node %d Information**********\n", _KEY(pRBNode));
	if (_COLOR(pRBNode) == _BLACK)
	{
		ESS_DEBUG_PRINTF("Color = Black\n");
	} 
	else
	{
		ESS_DEBUG_PRINTF("Color = Red\n");
	}

	if (pRBNode == _ROOT(pRBTree))
	{
		ESS_DEBUG_PRINTF("Parent = Header (I am root)\n");
	}
	else
	{
		ESS_DEBUG_PRINTF("Parent = %d\n", _KEY(_PARENT(pRBNode)));
	}

	if (_LEFT(pRBNode) == NULL)
	{
		ESS_DEBUG_PRINTF("Left = NULL\n");
	} 
	else
	{
		ESS_DEBUG_PRINTF("Left = %d\n", _KEY(_LEFT(pRBNode)));
	}

	if (_RIGHT(pRBNode) == NULL)
	{
		ESS_DEBUG_PRINTF("Right = NULL\n");
	} 
	else
	{
		ESS_DEBUG_PRINTF("Right = %d\n", _KEY(_RIGHT(pRBNode)));
	}

	ESS_DEBUG_PRINTF("*********End of Node Information**********\n");
}


/** 
 * _RBT_InorderPrintNode prints node information of RBTree in in-order way
 * 
 * @param pRBTree 	: A RBTree
 * @param pRBNode 	: start node to be printed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_RBT_InorderPrintNode(EssRBTree* pRBTree, EssRBNode* pRBNode)
{
	ESS_ASSERT(pRBTree);

	if (pRBNode != NULL)
	{
		_RBT_InorderPrintNode(pRBTree, _LEFT(pRBNode));
		_RBT_PrintNodeInfo(pRBTree, pRBNode);
		_RBT_InorderPrintNode(pRBTree, _RIGHT(pRBNode));
	}
}

/** 
 * EssRBTree_Print prints a RBTree node information
 * 
 * @param pRBTree 	: A RBTree
 * 
 * @return FFAT_OK	: Success
 * @return ESS_EINVALID	: Invalid parameters
 * 
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
t_int32 
EssRBTree_Print(EssRBTree* pRBTree)
{
#ifdef ESS_RBT_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree);

	ESS_DEBUG_PRINTF("********************Begin Tree Information*******************\n");
	ESS_DEBUG_PRINTF("Root = %d\n", _KEY(_ROOT(pRBTree)));
	ESS_DEBUG_PRINTF("Left Most = %d\n", _KEY(_LEFT_MOST(pRBTree)));
	ESS_DEBUG_PRINTF("Right Most = %d\n", _KEY(_RIGHT_MOST(pRBTree)));

	_RBT_InorderPrintNode(pRBTree, _ROOT(pRBTree));

	ESS_DEBUG_PRINTF("********************End Tree Information********************\n");

	return ESS_OK;
}
// debug end
