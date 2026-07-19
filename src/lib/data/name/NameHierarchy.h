#ifndef NAME_HIERARCHY_H
#define NAME_HIERARCHY_H

#include "SrctrlModule.h"

// Provided by the header in the classic build; in the module build the srctrl.data:name wrapper
// supplies them (NameElement in the same partition, NameDelimiterType via `import :types`), so skip
// them in the purview. (logging.h / utilityMainFunction.h are needed only by the out-of-line
// deserialize -- they live in NameHierarchy.cpp, not here.)
#ifndef SRCTRL_MODULE_PURVIEW
#include "NameDelimiterType.h"
#include "NameElement.h"

#include <sstream>
#include <string>
#include <vector>
#endif

SRCTRL_EXPORT class NameHierarchy
{
public:
	static std::string serialize(const NameHierarchy &nameHierarchy);
	static std::string serializeRange(const NameHierarchy &nameHierarchy, std::size_t first, std::size_t last);
	static NameHierarchy deserialize(const std::string &serializedName);

	NameHierarchy(const NameDelimiterType delimiterType = NameDelimiterType::UNKNOWN);
	NameHierarchy(std::string name, const NameDelimiterType delimiterType);
	~NameHierarchy() = default;

	NameHierarchy(const NameHierarchy &other) = default;
	NameHierarchy(NameHierarchy &&other) = default;
	NameHierarchy &operator=(const NameHierarchy &other) = default;
	NameHierarchy &operator=(NameHierarchy &&other) = default;

	void setDelimiter(std::string delimiter);
	const std::string &getDelimiter() const;

	void push(NameElement element);
	void push(std::string name);
	void pop();

	NameElement &back();
	const NameElement &back() const;

	NameHierarchy getRange(std::size_t first, std::size_t last) const;

	std::size_t size() const;

	std::string getQualifiedName() const;
	std::string getQualifiedNameWithSignature() const;
	std::string getRawName() const;
	std::string getRawNameWithSignature() const;
	std::string getRawNameWithSignatureParameters() const;

	bool hasSignature() const;
	NameElement::Signature getSignature() const;

private:
	NameHierarchy(std::string delimiter);
	NameHierarchy(std::string name, std::string delimiter);

	const NameElement &operator[](std::size_t pos) const;

	std::vector<NameElement> m_elements;
	std::string m_delimiter;
};

#include "NameHierarchy.inl"

#endif
