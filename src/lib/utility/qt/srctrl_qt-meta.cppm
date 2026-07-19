// `srctrl.qt:meta` partition -- the QMetaType reflection system, split out from `:core` so a consumer that
// only needs the metatype machinery (e.g. Id, via Q_DECLARE_METATYPE) doesn't pull the string types.
// NB: Q_DECLARE_METATYPE is a *macro* and can't cross an import -- a consumer keeps that macro textual and
// gets the QMetaType/QMetaTypeId machinery from this import.

module;

#include <QMetaType>

export module srctrl.qt:meta;

export
{
	using ::QMetaType;
}
