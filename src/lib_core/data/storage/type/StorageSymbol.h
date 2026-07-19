#ifndef STORAGE_SYMBOL_H
#define STORAGE_SYMBOL_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "DefinitionKind.h"
#include "Id.h"
#endif

SRCTRL_EXPORT struct StorageSymbol
{
	StorageSymbol(): id(0), definitionKind(DefinitionKind::NONE) {}

	StorageSymbol(Id id, DefinitionKind definitionKind): id(id), definitionKind(definitionKind) {}

	Id id;
	DefinitionKind definitionKind;
};

#endif	  // STORAGE_SYMBOL_H
