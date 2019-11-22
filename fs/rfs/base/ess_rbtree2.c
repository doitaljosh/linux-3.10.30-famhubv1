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
 * @file		ess_rbtree2.c
 * @brief		The file implements for ESS_RBTree module 
 * @author		Zhang Qing(qing1.zhang@samsung.com)
 * @version		JUL-26-2006 [Zhang Qing] First writing
 * @version		OCT-30-2006 [DongYoung Seo] : re-write for FFAT Free Cluster Cache
 * @see			None
 */

// debug begin
#include "ess_debug.h"
// debug end

// include for ESS 
#include "ess_types.h"
#include "ess_rbtree2.h"

// enum
enum _RBNodeColor
{
	_RED	= 0,
	_BLACK	= 1
};

// define
#define _KEY						ESS_RBTREE2_NODE_KEY

#define _COLOR(_pRBNode)			((_pRBNode)->dwRBNodeColor)
#define _LEFT(_pRBNode)				((_pRBNode)->pstRBNodeLeft)
#define _RIGHT(_pRBNode)			((_pRBNode)->pstRBNodeRight)
#define _PARENT(_pRBNode)			((_pRBNode)->pstRBNodeParent)

#define _GRAND_PARENT(_pRBNode)		(_PARENT(_PARENT(_pRBNode)))

#define _HEADER(_pRBTree)			((EssRBNode2*)(_pRBTree))
#define _ROOT(_pRBTree)				((_HEADER(_pRBTree))->pstRBNodeParent)
#define _LEFT_MOST(_pRBTree)		((_HEADER(_pRBTree))->pstRBNodeLeft)
#define _RIGHT_MOST(_pRBTree)		((_HEADER(_pRBTree))->pstRBNodeRight)
#ifdef ESS_RBT2_DEBUG
#define _BLACK_NUM(_pRBNode)		((_pRBNode)->dwBlackNum)
#endif

static t_boolean	_NodeIsLeftChild(EssRBNode2* pRBNode);
static t_boolean	_NodeIsRightChild(EssRBNode2* pRBNode);
static t_boolean	_NodeParentIsLeftChild(EssRBNode2* pRBNode);
static void			_LeftRotate(EssRBTree2* pRBTree, EssRBNode2* pRBNode);
static void			_RightRotate(EssRBTree2* pRBTree, EssRBNode2* pRBNode);
static void			_DelNodeWithSucReplace(EssRBTree2* pRBTree, EssRBNode2* pDelNode,
							EssRBNode2* pSucNode, EssRBNode2** ppNodeToFix,
							EssRBNode2** ppNodeToFixParent);
static void			_DelNodeDirectly(EssRBTree2* pRBTree, EssRBNode2* pDelNode,
							 EssRBNode2** ppNodeToFix,EssRBNode2** ppNodeToFixParent);
static void			_Delete(EssRBTree2* pRBTree, EssRBNode2* pNodeToDelete, 
							EssRBNode2** ppNodeToFix,EssRBNode2** ppNodeToFixParent);
static void			_ReBalanceForDelete(EssRBTree2* pRBTree, EssRBNode2* pNodeToFix,
							EssRBNode2* pNodeToFixParent);
static EssRBNode2*	_FindInsertPoint(EssRBTree2* pRBTree, EssRBNode2* pNodeToInsert);
static void			_Insert(EssRBTree2* pRBTree, EssRBNode2* pNodeToInsert,
							EssRBNode2* pInsertPoint);
static void			_ReBalance(EssRBTree2* pRBTree, EssRBNode2* pRBNode);

static t_int32		_traverseCount(EssRBTree2* pRBTree, EssRBNode2* pRBNode);

/**
* Initialize a RBTree
*
* @param		pRBTree		: [in] The header node of a RBTree, which represents the RBTree
* @return		ESS_OK		: Init success
* @return		ESS_EINVALID: Invalid RBTree
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
t_int32 
EssRBTree2_Init(EssRBTree2* pRBTree)
{
	EssRBNode2* pHeader;

#ifdef ESS_RBT2_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree);

	pHeader = _HEADER(pRBTree);

	_COLOR(pHeader)			= _RED;
	_PARENT(pHeader)		= NULL;
	_LEFT(pHeader)			= NULL;
	_RIGHT(pHeader)			= NULL;

#ifdef ESS_RBT2_DEBUG
	_BLACK_NUM(pHeader)		= 0;
#endif

	_LEFT_MOST(pRBTree)		= pHeader;
	_RIGHT_MOST(pRBTree)	= pHeader;

	return ESS_OK;
}

/**
* Insert a node to RBTree
*
* @param		pRBTree		: A RBTree 
* @param		pRBNode		: Node to be inserted (key must be assigned to pRBNode)
* @return		ESS_OK		: Insert success
* @return		ESS_EINVALID: Invalid RBTree or node
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
t_int32 
EssRBTree2_Insert(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	EssRBNode2*		pInsertPoint;

#ifdef ESS_RBT2_STRICT_CHECK
	if ((pRBTree == NULL) || (pRBNode == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree && pRBNode);

	pInsertPoint = _FindInsertPoint(pRBTree, pRBNode);
	_Insert(pRBTree, pRBNode, pInsertPoint);
	_ReBalance(pRBTree, pRBNode);

#ifdef ESS_RBT2_DEBUG
	EssRBTree2_Check(pRBTree);
#endif

	return ESS_OK;
}


/**
* Lookup a node which has given key from RBTree
*
* @param		pRBTree		: [in] A RBTree 
* @param		dwRBNodeKey	: [in] The key to be looked up 
* @return		pRBNode		: The node which has given key. 
* @return		NULL		: if there does not exist the node
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
EssRBNode2*
EssRBTree2_Lookup(EssRBTree2* pRBTree, t_uint32 dwRBNodeKey)
{
	EssRBNode2* pNodeTmp;
	EssRBNode2* pPossibleNode;

#ifdef ESS_RBT2_STRICT_CHECK
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


/**
* Lookup a node it has equal or bigger approximate value
*
* @param		pRBTree		: A RBTree
* @param		dwRBNodeKey	: The key to be looked up 
* @return		pRBNode		: The node which has given key. 
* @return		NULL		: if there does not exist the node
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
* @version		OCT-31-2006 [DongYoung Seo] modify EssRBTree2_Lookup() for approximate value lookup
* @version		SEP-04-2006 [Soojeong Kim] bug fix(같은 것이 있는데 못 나옴)
*/
EssRBNode2*
EssRBTree2_LookupBiggerApproximate(EssRBTree2* pRBTree, t_uint32 dwRBNodeKey)
{
	EssRBNode2* pNodeTmp;
	EssRBNode2* pPossibleNode;

#ifdef ESS_RBT2_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return NULL;
	}
#endif

	ESS_ASSERT(pRBTree);

	pPossibleNode	= NULL;
	pNodeTmp		= _ROOT(pRBTree);

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

	if ((pPossibleNode == NULL) || (_KEY(pPossibleNode) < dwRBNodeKey))
	{
		return NULL;
	}

	return pPossibleNode;
}


/**
* Lookup a node it has the equal or smaller approximate value
*
* @param		pRBTree		: A RBTREE
* @param		dwRBNodeKey	: The key to be looked up 
* @return		pRBNode	: The node which has given key. 
* @return		NULL	: if there does not exist the node
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
* @version		OCT-31-2006 [DongYoung Seo] modify EssRBTree2_Lookup() for approximate value lookup
* @version		AUG-22-2007 [Soojeong Kim] modify EssRBTree2_LookupBiggerApproximate() 
*/
EssRBNode2*
EssRBTree2_LookupSmallerApproximate(EssRBTree2* pRBTree, t_uint32 dwRBNodeKey)
{
	EssRBNode2* pNodeTmp;
	EssRBNode2* pPossibleNode;

#ifdef ESS_RBT2_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return NULL;
	}
#endif

	ESS_ASSERT(pRBTree);

	pPossibleNode	= NULL;
	pNodeTmp		= _ROOT(pRBTree);

	while (pNodeTmp != NULL)
	{
		if (dwRBNodeKey >= _KEY(pNodeTmp))
		{
			pPossibleNode = pNodeTmp;
			pNodeTmp = _RIGHT(pNodeTmp);
		}
		else
		{
			pNodeTmp = _LEFT(pNodeTmp);
		}
	}

	if ((pPossibleNode == NULL) || (_KEY(pPossibleNode) > dwRBNodeKey))
	{
		return NULL;
	}

	return pPossibleNode;
}


/**
 * Delete a node from RBTREE.
 * If delete success (return value is ESS_OK), the caller of EssRBTree_Delete should free pRBNode by itself.
 * It cannot be freed at here.
 * @param		pRBTree		: [in] A RBTREE 
 * @param		pRBNode		: [in] Node to be deleted (key must be assigned to pRBNode)
 * @return		ESS_OK		: Delete success
 * @return		ESS_EINVALID: Invalid RBTREE or node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
t_int32 
EssRBTree2_Delete(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	EssRBNode2* pNodeToFix;
	EssRBNode2* pNodeToFixParent;

#ifdef ESS_RBT2_STRICT_CHECK
	if ((pRBTree == NULL) || (pRBNode == NULL))
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree && pRBNode);

	pNodeToFix = NULL;
	pNodeToFixParent = NULL;

	_Delete(pRBTree, pRBNode, &pNodeToFix, &pNodeToFixParent);
	if (_COLOR(pRBNode) == _BLACK)
	{
		_ReBalanceForDelete(pRBTree, pNodeToFix, pNodeToFixParent);
	}

#ifdef ESS_RBT2_DEBUG
	EssRBTree2_Check(pRBTree);
#endif

	return ESS_OK;
}



/**
* check empty a RBTree
*
* @param		pRBTree		: A RBTree 
* @return		ESS_TRUE	: empty tree
* @return		ESS_FALSE	: not empty
* @author		DongYoung Seo
* @version		NOV-03-2006 [DongYoung Seo] First Writing
*/
t_boolean 
EssRBTree2_IsEmpty(EssRBTree2* pRBTree)
{
#ifdef ESS_RBT2_STRICT_CHECK
	if (pRBTree == NULL)
	{
		ESS_LOG_PRINTF("Invalid parameter !!");
		return ESS_EINVALID;
	}
#endif

	ESS_ASSERT(pRBTree);

	if ((_LEFT_MOST(pRBTree) == _HEADER(pRBTree)) &&
		(_RIGHT_MOST(pRBTree) == _HEADER(pRBTree)))
	{
		return ESS_TRUE;
	}

	return ESS_FALSE;
}


/**
*	get count of node in RBTree
*
*	Caution !!
*		This function may use much memory by recursive implementation
*		You must check stack usage 
*
* @param	pRBTree			: A RBTree
* @return	0 or positive	: count of node
* @return	else			: error
* @author	DongYoung Seo
* @version	26-NOV-2008 [DongYoung Seo] First Writing
*/
t_int32
EssRBTree2_Count(EssRBTree2* pRBTree)
{
	ESS_ASSERT(pRBTree);

	return _traverseCount(pRBTree, _ROOT(pRBTree));
}


//=============================================================================
//
//	static functions
//

/** 
 * _NodeIsLeftChild judges whether a node is left node or not
 * 
 * @param pRBNode 	: [in] A node pointer
 * 
 * @return FFAT_TRUE	: Node is left node
 * @return FFAT_FALSE	: Node is not left node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static t_boolean
_NodeIsLeftChild(EssRBNode2* pRBNode)
{
	ESS_ASSERT(pRBNode);

	return ((pRBNode == _LEFT(_PARENT(pRBNode))) ?
			ESS_TRUE : ESS_FALSE);
}

/** 
 * _NodeIsRightChild judges whether a node is right node or not
 * 
 * @param pRBNode 	: [in] A node pointer
 * 
 * @return FFAT_TRUE	: Node is right node
 * @return FFAT_FALSE	: Node is not right node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static t_boolean
_NodeIsRightChild(EssRBNode2* pRBNode)
{
	ESS_ASSERT(pRBNode);

	return ((pRBNode == _RIGHT(_PARENT(pRBNode))) ?
			ESS_TRUE : ESS_FALSE);
}

/** 
 * _NodeParentIsLeftChild judges whether a node's parent is left node or not
 * 
 * @param pRBNode 	: [in] A node pointer
 * 
 * @return FFAT_TRUE	: Node is left node
 * @return FFAT_FALSE	: Node is not left node
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static t_boolean
_NodeParentIsLeftChild(EssRBNode2* pRBNode)
{
	ESS_ASSERT(pRBNode);

	return (_NodeIsLeftChild(_PARENT(pRBNode)));
}

/**
* Left rotate a RBTree on node pRBNode
*
* @param		pRBTree		: [in] The header node of a RBTree
* @param		pRBNode		: [in] Node be left rotated
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
static void
_LeftRotate(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	EssRBNode2* pOldRight;

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
	else if (_NodeIsLeftChild(pRBNode))
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
* Right rotate a RBTree on node pRBNode
*
* @param		pRBTree		: [in] The header node of a RBTree
* @param		pRBNode		: [in] Node be right rotated
* @author		ZhangQing
* @version		JUL-26-2006 [ZhangQing] First Writing.
*/
static void
_RightRotate(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	EssRBNode2* pOldLeft;

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
	else if (_NodeIsRightChild(pRBNode))
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
 * _DelNodeWithSucReplace replaces pDelNode with pSucNode. It looks like original pSucNode is deleted.
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
_DelNodeWithSucReplace(EssRBTree2* pRBTree,
							EssRBNode2* pDelNode,
							EssRBNode2* pSucNode,
							EssRBNode2** ppNodeToFix,
							EssRBNode2** ppNodeToFixParent)
{
	EssRBNode2*		pSucNodeChild;
	t_boolean		bSucIsDelNodeChild;
	t_uint32		dwColorTmp;

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
	else if (_NodeIsLeftChild(pDelNode))
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
 * _DelNodeDirectly deletes a node from RBTree directly
 * 
 * @param pRBTree 				: [in] A RBTree
 * @param pNodeToDelete 		: [in] Node to be deleted
 * @param ppNodeToFix 			: [out] Node to be fixed
 * @param ppNodeToFixParent 	: [out] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_DelNodeDirectly(EssRBTree2* pRBTree, 
					 EssRBNode2* pDelNode,
					 EssRBNode2** ppNodeToFix,
					 EssRBNode2** ppNodeToFixParent)
{
	EssRBNode2*		pDelNodeChild;
	EssRBNode2*		pNodeTmp;

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

	if (pDelNodeChild != NULL)
	{
		_PARENT(pDelNodeChild) = _PARENT(pDelNode);
	}

	if (_ROOT(pRBTree) == pDelNode)
	{
		_ROOT(pRBTree) = pDelNodeChild;
	}
	else 
	{
		//Connect _PARENT(pDelNode) with pDelNodeChild
		if (_NodeIsLeftChild(pDelNode))
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
			pNodeTmp = pDelNodeChild;
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
			pNodeTmp = pDelNodeChild;
			while (_RIGHT(pNodeTmp) != NULL) 
				pNodeTmp = _RIGHT(pNodeTmp);
			_RIGHT_MOST(pRBTree) = pNodeTmp;
		}
	}
}

/** 
 * _Delete deletes a node from RBTree
 * 
 * @param pRBTree 				: [in] A RBTree
 * @param pNodeToDelete 		: [in] Node to be deleted
 * @param ppNodeToFix 			: [out] Node to be fixed
 * @param ppNodeToFixParent 	: [out] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_Delete(EssRBTree2* pRBTree, 
			EssRBNode2* pNodeToDelete, 
			EssRBNode2** ppNodeToFix,
			EssRBNode2** ppNodeToFixParent)
{
	EssRBNode2*		pSuccessor = NULL;
	t_boolean		bDelNodeHas2Child;

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
		_DelNodeWithSucReplace(pRBTree, pNodeToDelete, pSuccessor, 
			ppNodeToFix, ppNodeToFixParent);
	}
	else
	{
		_DelNodeDirectly(pRBTree, pNodeToDelete, 
			ppNodeToFix, ppNodeToFixParent);
	}

	//*ppNodeToFix might be null, but *ppNodeToFixParent is not null
	ESS_ASSERT(*ppNodeToFixParent);
}

/** 
 * _ReBalanceForDelete re-balance a RBTree after deleting a node
 * 
 * @param pRBTree 			: [in] A RBTree
 * @param pNodeToFix 		: [in] Node to be fixed
 * @param pNodeToFixParent 	: [in] The parent node of node to be fixed
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_ReBalanceForDelete(EssRBTree2* pRBTree, 
						EssRBNode2* pNodeToFix,
						EssRBNode2* pNodeToFixParent)
{
	EssRBNode2*		pSiblingNode;

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
				_LeftRotate(pRBTree, pNodeToFixParent);
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
					_RightRotate(pRBTree, pSiblingNode);
					pSiblingNode = _RIGHT(pNodeToFixParent);
				}

				_COLOR(pSiblingNode) = _COLOR(pNodeToFixParent);
				_COLOR(pNodeToFixParent) = _BLACK;
				if (_RIGHT(pSiblingNode) != NULL)
				{
					_COLOR(_RIGHT(pSiblingNode)) = _BLACK;
				}
				_LeftRotate(pRBTree, pNodeToFixParent);
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
				_RightRotate(pRBTree, pNodeToFixParent);
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
					_LeftRotate(pRBTree, pSiblingNode);
					pSiblingNode = _LEFT(pNodeToFixParent);
				}

				_COLOR(pSiblingNode) = _COLOR(pNodeToFixParent);
				_COLOR(pNodeToFixParent) = _BLACK;
				if (_LEFT(pSiblingNode) != NULL)
				{
					_COLOR(_LEFT(pSiblingNode)) = _BLACK;
				}
				_RightRotate(pRBTree, pNodeToFixParent);
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
 * _FindInsertPoint finds the insert point to insert a node into RBTree
 * 
 * @param pRBTree 			: A RBTree
 * @param pNodeToInsert 	: Node to be inserted
 * 
 * @return Insert point
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static EssRBNode2* 
_FindInsertPoint(EssRBTree2* pRBTree, 
					 EssRBNode2* pNodeToInsert)
{
	EssRBNode2*		pInsertPoint;
	EssRBNode2*		pNodeTmp;
	t_uint32		dwKey;

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
 * _Insert inserts a node into RBTree
 * 
 * @param pRBTree 			: A RBTree
 * @param pNodeToInsert 	: Node to be inserted
 * @param pInsertPoint 		: Insert point
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_Insert(EssRBTree2* pRBTree, 
			EssRBNode2* pNodeToInsert,
			EssRBNode2* pInsertPoint)
{
	EssRBNode2*		pHeader;

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
 * _ReBalance rebalances a RBTree after insert a node
 * 
 * @param pRBTree 	: A RBTree
 * @param pRBNode 	: Node be inserted
 * @author		ZhangQing
 * @version		JUL-26-2006 [ZhangQing] First Writing.
 */
static void
_ReBalance(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	EssRBNode2*		pUncle;

	ESS_ASSERT(pRBTree && pRBNode);

	_COLOR(pRBNode) = _RED;

	while ((pRBNode != _ROOT(pRBTree)) 
		&& (_COLOR(_PARENT(pRBNode)) == _RED))
	{
		if (_NodeParentIsLeftChild(pRBNode))
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
				if (_NodeIsRightChild(pRBNode))
				{
					pRBNode = _PARENT(pRBNode);
					_LeftRotate(pRBTree, pRBNode);
				}
				_COLOR(_PARENT(pRBNode)) = _BLACK;
				_COLOR(_GRAND_PARENT(pRBNode)) = _RED;
				_RightRotate(pRBTree, _GRAND_PARENT(pRBNode));
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
				if (_NodeIsLeftChild(pRBNode))
				{
					pRBNode = _PARENT(pRBNode);
					_RightRotate(pRBTree, pRBNode);
				}
				_COLOR(_PARENT(pRBNode)) = _BLACK;
				_COLOR(_GRAND_PARENT(pRBNode)) = _RED;
				_LeftRotate(pRBTree, _GRAND_PARENT(pRBNode));
			}
		}
	}

	_COLOR(_ROOT(pRBTree)) = _BLACK;
}


/**
*	traverse all nodes in the tree for count of node
*
* @param	pRBTree			: A RBTree
* @return	0 or positive	: count of node
* @return	else			: error
* @author	DongYoung Seo
* @version	26-NOV-2008 [DongYoung Seo] First Writing
*/
static t_int32
_traverseCount(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	t_int32		dwCount = 0;

	ESS_ASSERT(pRBTree);

	if (pRBNode != NULL)
	{
		dwCount++;
		dwCount += _traverseCount(pRBTree, _LEFT(pRBNode));
		dwCount += _traverseCount(pRBTree, _RIGHT(pRBNode));
	}

	return dwCount;
}


// debug begin
//=============================================================================
//
//	FOR DEBUG
//
#ifdef ESS_RBT2_DEBUG

static void	_PrintNodeInfo(EssRBTree2* pRBTree, EssRBNode2* pRBNode);
static void	_InorderPrintNode(EssRBTree2* pRBTree, EssRBNode2* pRBNode);

t_int32
EssRBTree2_Print(EssRBTree2* pRBTree)
{
#ifdef ESS_RBT2_STRICT_CHECK
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

	_InorderPrintNode(pRBTree, _ROOT(pRBTree));

	ESS_DEBUG_PRINTF("********************End Tree Information********************\n");

	return ESS_OK;
}


static void
_PrintNodeInfo(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
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


static void
_InorderPrintNode(EssRBTree2* pRBTree, EssRBNode2* pRBNode)
{
	ESS_ASSERT(pRBTree);

	if (pRBNode != NULL)
	{
		_InorderPrintNode(pRBTree, _LEFT(pRBNode));
		_PrintNodeInfo(pRBTree, pRBNode);
		_InorderPrintNode(pRBTree, _RIGHT(pRBNode));
	}
}


static t_int32 _gdwBlackNum;

static void
_DFSRBTree(EssRBNode2* pRBNode)
{
	if (pRBNode == NULL)
	{
		return;
	}

	if (_COLOR(pRBNode) == _BLACK)
	{
		_BLACK_NUM(pRBNode) = _BLACK_NUM(_PARENT(pRBNode)) + 1;
	}
	else
	{
		_BLACK_NUM(pRBNode) = _BLACK_NUM(_PARENT(pRBNode));

		if (_LEFT(pRBNode) != NULL)
		{
			ESS_ASSERT(_COLOR(_LEFT(pRBNode)) == _BLACK);
		}

		if (_RIGHT(pRBNode) != NULL)
		{
			ESS_ASSERT(_COLOR(_RIGHT(pRBNode)) == _BLACK);
		}
	}

	if (_LEFT(pRBNode) != NULL)
	{
		ESS_ASSERT(_KEY(_LEFT(pRBNode)) <= _KEY(pRBNode));
	}

	if (_RIGHT(pRBNode) != NULL)
	{
		ESS_ASSERT(_KEY(_RIGHT(pRBNode)) >= _KEY(pRBNode));
	}

	if ( (_LEFT(pRBNode) == NULL) && (_RIGHT(pRBNode) == NULL) ) //leaf node
	{
		if (_gdwBlackNum == -1)
		{
			_gdwBlackNum = _BLACK_NUM(pRBNode);
		}
		else
		{
			ESS_ASSERT(_gdwBlackNum == _BLACK_NUM(pRBNode));
		}
		return;
	}

	_DFSRBTree(_LEFT(pRBNode));
	_DFSRBTree(_RIGHT(pRBNode));
}

t_int32
EssRBTree2_Check(EssRBTree2* pRBTree)
{
	_gdwBlackNum = -1;
	_DFSRBTree(_ROOT(pRBTree));
	return ESS_OK;
}

#endif
// debug end
