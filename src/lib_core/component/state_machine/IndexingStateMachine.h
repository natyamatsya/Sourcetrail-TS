#ifndef INDEXING_STATE_MACHINE_H
#define INDEXING_STATE_MACHINE_H

#include <condition_variable>
#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "RefreshInfo.h"

enum class DatabasePolicy
{
	DATABASE_POLICY_KEEP,
	DATABASE_POLICY_DISCARD,
	DATABASE_POLICY_REFRESH,
	DATABASE_POLICY_UNKNOWN
};

struct IndexingReport
{
	std::size_t indexedFileCount = 0;
	std::size_t totalIndexedFileCount = 0;
	std::size_t completedFileCount = 0;
	std::size_t totalFileCount = 0;
	float time = 0;
	std::size_t errorCount = 0;
	std::size_t fatalCount = 0;
	bool interrupted = false;
};

// Shows the indexing states that wait for a person: the mode choice ("Start
// Indexing") and the report. Whatever the presenter shows, its buttons go to the
// IndexingStateMachine, which an agent drives the same way.
class IndexingPresenter
{
public:
	virtual ~IndexingPresenter() = default;

	virtual void presentModeChoice(const std::vector<RefreshMode>& modes, RefreshMode mode) = 0;
	// The files the selected mode would index are being counted ...
	virtual void presentCounting() = 0;
	// ... and are known.
	virtual void presentRefreshInfo(const RefreshInfo& info) = 0;
	// Someone else (an agent) chose; close the choice. `started`: indexing goes on.
	virtual void dismissModeChoice(bool started) = 0;

	// Shows the report, or returns false when nobody could answer it (no GUI, or
	// offscreen): the run then keeps its data.
	virtual bool presentReport(const IndexingReport& report) = 0;
	// Someone else decided; close the report.
	virtual void dismissReport() = 0;
};

// The indexing lifecycle, from a refresh request to the decision what to do with
// the result. The pipeline (Project and its tasks) reports where it is; the user's
// dialogs and agents send the decisions.
//
//   idle --refresh--> pending --ask_mode--> choosing_mode --start--> pending
//   pending --prepare--> preparing --begin--> indexing --wrap_up/interrupt--> finishing
//   indexing/finishing --report--> reporting --keep/discard/reindex--> finishing
//   any --end--> idle; choosing_mode --cancel--> idle
//
// "pending" is the pipeline deciding what comes next: it may ask a question first
// (see QuestionStateMachine), find nothing to do, or need the mode chosen.
struct IndexingMachine
{
	enum class State
	{
		Idle,
		Pending,
		ChoosingMode,
		Preparing,
		Indexing,
		Finishing,
		Reporting
	};
	enum class Event
	{
		// Sent by the dialogs or an agent.
		Refresh,
		SelectMode,
		Start,
		Cancel,
		Interrupt,
		Keep,
		Discard,
		Reindex,
		// Sent by the pipeline.
		Requested,
		AskMode,
		Counted,
		Prepare,
		Begin,
		WrapUp,
		Report,
		End
	};

	static constexpr std::string_view name = "indexing";
	static constexpr State initial = State::Idle;
	static constexpr std::array states {
		named(State::Idle, "idle"),
		named(State::Pending, "pending"),
		named(State::ChoosingMode, "choosing_mode"),
		named(State::Preparing, "preparing"),
		named(State::Indexing, "indexing"),
		named(State::Finishing, "finishing"),
		named(State::Reporting, "reporting")};
	static constexpr std::array events {
		control(Event::Refresh, "refresh"),
		control(Event::SelectMode, "select_mode"),
		control(Event::Start, "start"),
		control(Event::Cancel, "cancel"),
		control(Event::Interrupt, "interrupt"),
		control(Event::Keep, "keep"),
		control(Event::Discard, "discard"),
		control(Event::Reindex, "reindex"),
		named(Event::Requested, "requested"),
		named(Event::AskMode, "ask_mode"),
		named(Event::Counted, "counted"),
		named(Event::Prepare, "prepare"),
		named(Event::Begin, "begin"),
		named(Event::WrapUp, "wrap_up"),
		named(Event::Report, "report"),
		named(Event::End, "end")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Idle, Event::Refresh, State::Pending},
		{State::Idle, Event::Requested, State::Pending},

		{State::Pending, Event::AskMode, State::ChoosingMode},
		{State::Pending, Event::Prepare, State::Preparing},
		{State::Pending, Event::End, State::Idle},

		{State::ChoosingMode, Event::SelectMode, State::ChoosingMode},
		{State::ChoosingMode, Event::Counted, State::ChoosingMode},
		{State::ChoosingMode, Event::Start, State::Pending},
		{State::ChoosingMode, Event::Cancel, State::Idle},
		{State::ChoosingMode, Event::End, State::Idle},

		{State::Preparing, Event::Begin, State::Indexing},
		{State::Preparing, Event::Interrupt, State::Finishing},
		{State::Preparing, Event::WrapUp, State::Finishing},
		{State::Preparing, Event::Report, State::Reporting},
		{State::Preparing, Event::End, State::Idle},

		{State::Indexing, Event::Interrupt, State::Finishing},
		{State::Indexing, Event::WrapUp, State::Finishing},
		{State::Indexing, Event::Report, State::Reporting},
		{State::Indexing, Event::End, State::Idle},

		{State::Finishing, Event::Report, State::Reporting},
		{State::Finishing, Event::End, State::Idle},

		{State::Reporting, Event::Keep, State::Finishing},
		{State::Reporting, Event::Discard, State::Finishing},
		{State::Reporting, Event::Reindex, State::Finishing},
		{State::Reporting, Event::End, State::Idle},
	});
};

class IndexingStateMachine: public ConcurrentStateMachine<IndexingMachine>
{
public:
	static std::shared_ptr<IndexingStateMachine> instance();

	// The pipeline's side.

	// A refresh reached the application (the menu, a project load, or `refresh`) ...
	void requested();
	// ... and returned. Back to idle unless it moved on (`outcome` says why not).
	void requestHandled(const std::string& outcome);
	// Opens the mode choice. `countFiles` says what a mode would index; it runs on
	// the app scheduler.
	void chooseMode(
		std::function<RefreshInfo(RefreshMode)> countFiles,
		const std::vector<RefreshMode>& modes,
		RefreshMode initialMode,
		std::function<void(const RefreshInfo&)> onStart,
		std::function<void()> onCancel,
		IndexingPresenter* presenter);
	void preparing();
	void indexingStarted();
	void progress(std::size_t started, std::size_t finished, std::size_t total, const std::string& file);
	void wrappingUp();
	// Waits for the decision on the report, from the presenter's buttons or an agent.
	DatabasePolicy report(const IndexingReport& report, IndexingPresenter* presenter);
	// The run or request is over; an empty `outcome` keeps the report's decision.
	void ended(const std::string& outcome);
	// The presenter goes away: forget it, and let a report waiting on it go on.
	void detach(IndexingPresenter* presenter);

	// The controls: what the dialogs' buttons and agents send.

	MachineFireResult refresh(RefreshMode mode, EventSource source);
	MachineFireResult selectMode(RefreshMode mode, EventSource source);
	MachineFireResult start(std::optional<RefreshMode> mode, EventSource source);
	MachineFireResult cancel(EventSource source);
	MachineFireResult interrupt(EventSource source);
	MachineFireResult decide(DatabasePolicy policy, EventSource source);

	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) override;

	static const char* modeName(RefreshMode mode);
	static std::optional<RefreshMode> modeFromName(const std::string& name);

private:
	struct Progress
	{
		std::size_t started = 0;
		std::size_t finished = 0;
		std::size_t total = 0;
		std::string file;
	};

	void count(RefreshMode mode);
	void counted(RefreshMode mode, const RefreshInfo& info);
	void resetChoice();
	// Whether `mode` is offered in the open choice; the reason when not.
	std::expected<RefreshMode, std::string> offered(RefreshMode mode) const;
	std::string contextLocked() const override;

	std::condition_variable m_decided;

	// choosing_mode
	std::function<RefreshInfo(RefreshMode)> m_countFiles;
	std::vector<RefreshMode> m_modes;
	RefreshMode m_mode = RefreshMode::NONE;
	std::map<RefreshMode, RefreshInfo> m_infos;
	std::set<RefreshMode> m_counting;
	std::function<void(const RefreshInfo&)> m_onStart;
	std::function<void()> m_onCancel;
	IndexingPresenter* m_choicePresenter = nullptr;

	// indexing, finishing
	Progress m_progress;
	bool m_interrupted = false;

	// reporting
	std::optional<IndexingReport> m_report;
	std::optional<DatabasePolicy> m_decision;
	IndexingPresenter* m_reportPresenter = nullptr;

	std::string m_outcome;	// how the last request or run ended
};

#endif	  // INDEXING_STATE_MACHINE_H
