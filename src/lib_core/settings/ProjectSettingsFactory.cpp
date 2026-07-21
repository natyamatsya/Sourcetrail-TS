// Classic-build emission TU for the inline factory (see ProjectSettingsFactory.inl). The
// non-inline function below odr-uses it, forcing clang to emit the linkonce_odr definition in
// this object -- classic callers include only ProjectSettings.h and link against this emission.
// (A namespace-scope address-take is not enough: the unused internal variable is dropped before
// it forces the emission.)
#include "ProjectSettingsFactory.inl"

namespace srctrl_emission
{
std::vector<std::shared_ptr<SourceGroupSettings>> emitProjectSettingsFactory(
	const ProjectSettings& settings)
{
	return settings.getAllSourceGroupSettings();
}
}	 // namespace srctrl_emission
