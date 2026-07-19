// Most NameHierarchy members are inline in NameHierarchy.inl (included by the header). deserialize
// stays here because it needs LOG_ERROR (logging.h) and utilityMainFunction -- the latter
// forward-declares NameHierarchy, so it can't sit in the module's global fragment. deserialize is thus
// an include-only member (not reachable via `import srctrl.data;`), like the logging/Qt seams.
#include "NameHierarchy.h"

#include "logging.h"
#include "utilityMainFunction.h"

NameHierarchy NameHierarchy::deserialize(const std::string& serializedName)
{
	size_t mpos = serializedName.find(META_DELIMITER);
	if (mpos == std::string::npos)
	{
		LOG_ERROR("unable to deserialize name hierarchy: " + serializedName);
		return NameHierarchy(NameDelimiterType::UNKNOWN);
	}

	NameHierarchy nameHierarchy(serializedName.substr(0, mpos));

	size_t npos = mpos + META_DELIMITER.size();
	while (npos < serializedName.size())
	{
		// name
		size_t spos = serializedName.find(PART_DELIMITER, npos);
		if (spos == std::string::npos)
		{
			LOG_ERROR("unable to deserialize name hierarchy: " + serializedName);
			return NameHierarchy(NameDelimiterType::UNKNOWN);
		}

		std::string name = serializedName.substr(npos, spos - npos);
		spos += PART_DELIMITER.size();

		// signature
		size_t ppos = serializedName.find(SIGNATURE_DELIMITER, spos);
		if (ppos == std::string::npos)
		{
			LOG_ERROR("unable to deserialize name hierarchy: " + serializedName);
			return NameHierarchy(NameDelimiterType::UNKNOWN);
		}

		std::string prefix = serializedName.substr(spos, ppos - spos);
		ppos += SIGNATURE_DELIMITER.size();

		std::string postfix;
		npos = serializedName.find(NAME_DELIMITER, ppos);
		if (npos == std::string::npos)
		{
			postfix = serializedName.substr(ppos, std::string::npos);
		}
		else
		{
			postfix = serializedName.substr(ppos, npos - ppos);
			npos += NAME_DELIMITER.size();
		}

		nameHierarchy.push(NameElement(std::move(name), std::move(prefix), std::move(postfix)));
	}

	if (isUniquifiedMainFunction(nameHierarchy))
		deuniquifyMainFunction(&nameHierarchy);

	return nameHierarchy;
}
