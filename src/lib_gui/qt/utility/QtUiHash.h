#ifndef QT_UI_HASH_H
#define QT_UI_HASH_H

#include <cstdint>
#include <string_view>

#include <QtGlobal>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleActionInterface>

#include "QtAccessibleRole.h"

namespace utility::qt
{
namespace detail
{
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

inline void hashMixStr(std::uint64_t& h, std::string_view s)
{
	for (const unsigned char c: s)
	{
		h = (h ^ c) * kFnvPrime;
	}
	h = (h ^ 0xFEu) * kFnvPrime;	// field separator
}

inline void hashMixU64(std::uint64_t& h, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i)
	{
		h = (h ^ ((v >> (i * 8)) & 0xFFu)) * kFnvPrime;
	}
}
}	 // namespace detail

//! Merkle structural hash of a live accessible element's subtree: identity +
//! structure (role, name, action names, ordered child hashes) — NOT volatile
//! geometry / focus / value, so it fires on *meaningful* modification but survives
//! cosmetic churn. Used to detect that an element changed between snapshot and
//! action (optimistic control; DESIGN_AGENT_UI_CONTROL.md). The snapshot walk, the
//! query walk, and the execute-time recompute all call this, so their hashes agree.
//!
//! O(subtree) per call — fine for the on-demand snapshot and for a single
//! execute-time target on a shallow-wide UI tree.
inline std::uint64_t structuralHash(QAccessibleInterface* iface)
{
	using namespace detail;
	std::uint64_t h = kFnvOffset;
	hashMixStr(h, accessibleRoleName(iface->role()));
	hashMixStr(h, iface->text(QAccessible::Name).toStdString());
	if (QAccessibleActionInterface* actions = iface->actionInterface())
	{
		for (const QString& action: actions->actionNames())
		{
			hashMixStr(h, action.toStdString());
		}
	}
	h = (h ^ 0xFDu) * kFnvPrime;	// separator between own fields and children
	const int n = iface->childCount();
	for (int i = 0; i < n; ++i)
	{
		if (QAccessibleInterface* child = iface->child(i))
		{
			hashMixU64(h, structuralHash(child));
		}
	}
	return h;
}
}	 // namespace utility::qt
#endif	// QT_CONFIG(accessibility)

#endif	  // QT_UI_HASH_H
