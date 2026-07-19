#ifndef SINGLE_VALUE_CACHE_H
#define SINGLE_VALUE_CACHE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <functional>
#endif

SRCTRL_EXPORT template <typename ValType>
class SingleValueCache
{
public:
	SingleValueCache(std::function<ValType()> calculator);
	ValType getValue();
	void clear();

private:
	std::function<ValType()> m_calculator;
	ValType m_value;
	bool m_hasValue = false;
};

template <typename ValType>
SingleValueCache<ValType>::SingleValueCache(std::function<ValType()> calculator)
	: m_calculator(calculator) 
{
}

template <typename ValType>
ValType SingleValueCache<ValType>::getValue()
{
	if (!m_hasValue)
	{
		m_value = m_calculator();
		m_hasValue = true;
	}
	return m_value;
}

template <typename ValType>
void SingleValueCache<ValType>::clear()
{
	if (m_hasValue)
	{
		m_value = ValType();
		m_hasValue = false;
	}
}

#endif	  // SINGLE_VALUE_CACHE_H
