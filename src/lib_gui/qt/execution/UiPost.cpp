#include "UiPost.h"

#include "QtThreadScheduler.h"
#include "StdexecPrelude.h"
#include "logging.h"

#include <QCoreApplication>
#include <QThread>

namespace execution::qt
{
namespace
{
bool onGuiThread() noexcept
{
	const QCoreApplication* app = QCoreApplication::instance();
	return app != nullptr && QThread::currentThread() == app->thread();
}
}	 // namespace

void onUi(QObject* anchor, std::function<void()> work)
{
	if (onGuiThread())
	{
		work();	 // matches QtThreadedFunctor: run inline when already on the GUI thread
		return;
	}

	// Post onto the GUI thread anchored at `anchor`. The lambda is noexcept and
	// swallows exceptions so the sender has no error channel -- required by
	// start_detached, and consistent with ADR-0001 (domain errors are values,
	// not exceptions; a throwing GUI callback is a bug we log rather than crash).
	stdexec::start_detached(
		stdexec::schedule(QtThreadScheduler{ anchor }) |
		stdexec::then(
			[work = std::move(work)]() noexcept
			{
				try
				{
					work();
				}
				catch (const std::exception& e)
				{
					LOG_ERROR(std::string("onUi work threw: ") + e.what());
				}
				catch (...)
				{
					LOG_ERROR("onUi work threw an unknown exception");
				}
			}));
}
}	 // namespace execution::qt
