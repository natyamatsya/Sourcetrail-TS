#pragma once

// Qt-backed implementation of execution::ISchedulers:
//   - io()      : exec::static_thread_pool sized for I/O-bound work
//   - compute() : exec::static_thread_pool sized for CPU-bound work
//   - ui()      : posts to the Qt GUI thread (via QtThreadScheduler)
//
// Injectable (implements ISchedulers) with a process-wide singleton default
// (getInstance) so call sites stay simple until they migrate to injection.

#include "ISchedulers.h"

#include <memory>

namespace execution::qt
{
class Schedulers final : public execution::ISchedulers
{
public:
	//! Process-wide default. Prefer injecting execution::ISchedulers; use this
	//! at call sites not yet migrated to injection.
	static Schedulers &getInstance();

	[[nodiscard]] AnyScheduler io() const override;
	[[nodiscard]] AnyScheduler compute() const override;
	[[nodiscard]] AnyScheduler ui() const override;

	Schedulers(const Schedulers &) = delete;
	Schedulers &operator=(const Schedulers &) = delete;

private:
	//! @p ioThreads / @p computeThreads: 0 => sensible default (I/O: 2,
	//! Compute: hardware_concurrency with a floor of 2).
	Schedulers(unsigned ioThreads, unsigned computeThreads);
	~Schedulers() override;

	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
} // namespace execution::qt
