#ifndef STORAGE_LOCAL_SYMBOL_H
#define STORAGE_LOCAL_SYMBOL_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "types.h"
#endif

SRCTRL_EXPORT struct StorageLocalSymbolData
{
	StorageLocalSymbolData() = default;

	StorageLocalSymbolData(std::string name): name(std::move(name)) {}

	bool operator<(const StorageLocalSymbolData& other) const
	{
		return name < other.name;
	}

	std::string name;
};

SRCTRL_EXPORT struct StorageLocalSymbol: public StorageLocalSymbolData
{
	StorageLocalSymbol():  id(0) {}

	StorageLocalSymbol(Id id, const StorageLocalSymbolData& data)
		: StorageLocalSymbolData(data), id(id)
	{
	}

	StorageLocalSymbol(Id id, std::string name): StorageLocalSymbolData(std::move(name)), id(id) {}

	Id id;
};

#endif	  // STORAGE_LOCAL_SYMBOL_H
