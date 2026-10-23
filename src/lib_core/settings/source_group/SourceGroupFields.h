#ifndef SOURCE_GROUP_FIELDS_H
#define SOURCE_GROUP_FIELDS_H

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "SourceGroupType.h"

class SourceGroupSettings;

// A source group's settings as named fields: what the project wizard's machine shows
// and an agent sets (ProjectWizardStateMachine). A field belongs to the groups whose
// type has its component: source_paths to an empty C++ group, compilation_database to
// a compilation database's.
using SourceGroupField = std::variant<bool, std::string, std::vector<std::string>>;

// The fields `settings` has, with their values.
std::map<std::string, SourceGroupField> readSourceGroupFields(const SourceGroupSettings& settings);

// Sets field `name`; what is wrong otherwise (not a field of this group, a value of
// the wrong kind).
std::expected<void, std::string> writeSourceGroupField(
	SourceGroupSettings& settings, const std::string& name, const SourceGroupField& value);

// The source group types by the names agents use for them ("cdb", "cmake", "cpp", ...),
// and back; UNKNOWN for a name that is none.
std::string_view sourceGroupTypeId(SourceGroupType type);
SourceGroupType sourceGroupTypeForId(std::string_view id);

#endif	  // SOURCE_GROUP_FIELDS_H
