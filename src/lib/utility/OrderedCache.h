#ifndef ORDERED_CACHE_H
#define ORDERED_CACHE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <functional>
#include <map>
#endif

SRCTRL_EXPORT template <typename KeyType, typename ValType>
class OrderedCache
{
public:
	OrderedCache(std::function<ValType(const KeyType&)> calculator);
	ValType getValue(const KeyType& key);

private:
	std::function<ValType(const KeyType&)> m_calculator;
	std::map<KeyType, ValType> m_map;

	std::size_t m_hitCount = 0;
	std::size_t m_missCount = 0;
};

template <typename KeyType, typename ValType>
OrderedCache<KeyType, ValType>::OrderedCache(std::function<ValType(const KeyType&)> calculator)
	: m_calculator(calculator) 
{
}

template <typename KeyType, typename ValType>
ValType OrderedCache<KeyType, ValType>::getValue(const KeyType& key)
{
	auto it = m_map.find(key);
	if (it != m_map.end())
	{
		++m_hitCount;
		return it->second;
	}
	++m_missCount;
	ValType val = m_calculator(key);
	m_map.emplace(key, val);
	return val;
}

#endif	  // ORDERED_CACHE_H
