#pragma once

// Injectable service that hands out execution-context schedulers for
// sender/receiver pipelines. Kept as a pure interface (no Qt, no stdexec
// beyond the type-erased AnyScheduler) so non-GUI code and tests can depend on
// it and inject a fake. The production implementation lives in the Qt layer
// (src/lib_gui/qt/execution/Schedulers) because ui() targets the Qt GUI thread.

#include "SenderAliases.h"

namespace execution
{
//! Service interface handing out I/O, Compute and Ui schedulers.
class ISchedulers
{
public:
	virtual ~ISchedulers() = default;

	//! Scheduler for I/O-bound work (disk, network, IPC).
	[[nodiscard]] virtual AnyScheduler io() const = 0;

	//! Scheduler for CPU-bound work.
	[[nodiscard]] virtual AnyScheduler compute() const = 0;

	//! Scheduler that posts work to the Qt GUI thread. Completes via
	//! set_stopped if the application is torn down before the work runs.
	[[nodiscard]] virtual AnyScheduler ui() const = 0;
};
}	 // namespace execution
