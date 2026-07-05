#pragma once

// onUi(anchor, work): run `work` on the Qt GUI thread.
//
// Drop-in replacement for QtThreadedFunctor's `m_onQtThread([]{...})`:
//   - If the caller is already on the GUI thread, `work` runs inline.
//   - Otherwise it is posted (fire-and-forget) via the stdexec QtThreadScheduler
//     anchored at `anchor` -- a QObject that lives on the GUI thread and whose
//     lifetime bounds the work: if `anchor` is destroyed before the posted work
//     runs, the work is dropped (completes set_stopped), so no use-after-free.
//
// Anchor to the widget the work touches (e.g. the view's status bar / graph
// view), not to qApp, so per-view teardown is safe.

#include <functional>

class QObject;

namespace execution::qt
{
void onUi(QObject* anchor, std::function<void()> work);
}	 // namespace execution::qt
