#ifndef CXX_QUALIFIER_FLAGS_H
#define CXX_QUALIFIER_FLAGS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdint>
#include <string>
#endif

SRCTRL_EXPORT class CxxQualifierFlags
{
public:
	enum class QualifierType : std::uint8_t
	{
		NONE = 0,
		
		CONST     = 1 << 0,
		CONSTEXPR = 1 << 1
	};

	CxxQualifierFlags(QualifierType qualifier = QualifierType::NONE);
	
	void addQualifier(QualifierType qualifier);

	bool empty() const;
	std::string toString() const;

private:
	QualifierType m_flags = QualifierType::NONE;
};

#include "CxxQualifierFlags.inl"

#endif
