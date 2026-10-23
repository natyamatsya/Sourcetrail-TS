#ifndef MESSAGE_CAUSE_H
#define MESSAGE_CAUSE_H

#include <cstddef>
#include <cstdint>
#include <functional>
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

// The work queued on behalf of a request and not done yet: its messages from when
// they are queued until they are handled, the tasks scheduled for it until they
// end. A request with none left (and its views drawn) has settled: agent control's
// Settled, from the work's own completion rather than from schedulers going idle.
class CauseWork
{
public:
	static void begun(std::uint64_t cause);
	static void ended(std::uint64_t cause);
	static std::size_t pending(std::uint64_t cause);
	// Called with the cause, on the thread that ended the work, whenever a request's
	// pending work drops to none. One at a time; nullptr clears it. Returns once no
	// call of the one it replaces runs any more.
	static void onDone(std::function<void(std::uint64_t)> done);

	// Work on behalf of a cause (by default the current one) while it lives: a
	// pipeline, a question its request waits for. Move-only.
	class Held
	{
	public:
		Held(): Held(currentCause()) {}
		explicit Held(std::uint64_t cause): m_cause(cause)
		{
			begun(m_cause);
		}
		Held(Held&& other) noexcept: m_cause(std::exchange(other.m_cause, 0)) {}
		Held& operator=(Held&& other) noexcept
		{
			if (this != &other)
			{
				ended(std::exchange(m_cause, std::exchange(other.m_cause, 0)));
			}
			return *this;
		}
		Held(const Held&) = delete;
		Held& operator=(const Held&) = delete;
		~Held()
		{
			ended(m_cause);
		}

		std::uint64_t cause() const
		{
			return m_cause;
		}

	private:
		std::uint64_t m_cause;	// 0 once moved from: nothing held
	};
};

#endif	  // MESSAGE_CAUSE_H
