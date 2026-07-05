#pragma once

// Qt sender/receiver bridge. Originally by Ville Voutilainen (libunifex-qt),
// ported from libunifex to NVIDIA stdexec.
//-----------------------------------------------------------------------
/*!
 * @file	Concepts.h
 *
 * @brief	Concepts shared by the Qt bridge headers.
 */
//-----------------------------------------------------------------------

#include <QObject>

#include <concepts>

namespace execution::qt
{

//! Concept: @c T publicly derives from @c QObject.
template <class T>
concept QObjectDerived = std::derived_from<T, QObject>;

}; // namespace execution::qt
