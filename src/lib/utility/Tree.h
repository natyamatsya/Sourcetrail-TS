#ifndef TREE_H
#define TREE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>
#endif

SRCTRL_EXPORT template <typename T>
struct Tree
{
	Tree() = default;
	Tree(T data): data(data) {}
	T data;
	std::vector<Tree<T>> children;
};

#endif	  // TREE_H
