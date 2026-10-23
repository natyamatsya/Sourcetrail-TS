#ifndef MESSAGE_CAUSE_H
#define MESSAGE_CAUSE_H

#include <cstdint>
#include <utility>

// What a thread's work is on behalf of: an agent's request id, or 0 for nothing
// known (the user, the app on its own). A message records the cause current when it
// is dispatched, and its listeners run under it, so what they dispatch in turn
// carries it on: the activation an agent's command led to, the question its load
// asked, the transitions and events they produce. Events report it as `caused_by`.
//
// A thread's cause lasts for a CauseScope; work handed to another thread (a GUI
// post, a timer) takes it along with withCurrentCause().
class CauseScope
{
public:
	explicit CauseScope(std::uint64_t cause);
	~CauseScope();
	CauseScope(const CauseScope&) = delete;
	CauseScope& operator=(const CauseScope&) = delete;

private:
	const std::uint64_t m_previous;
};

std::uint64_t currentCause();

// `work`, to run later (on another thread, from a timer), on behalf of what the
// calling thread works for now.
template <typename Work>
auto withCurrentCause(Work work)
{
	return [work = std::move(work), cause = currentCause()]() {
		const CauseScope scope(cause);
		work();
	};
}

#endif	  // MESSAGE_CAUSE_H
