#ifndef INDEXING_STATE_MACHINE_H
#define INDEXING_STATE_MACHINE_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "RefreshInfo.h"
#include "RemoteStateMachine.h"
#include "StateMachine.h"

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
class IndexingStateMachine: public RemoteStateMachine
{
public:
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

	static std::shared_ptr<IndexingStateMachine> instance();

	IndexingStateMachine();

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

	std::string name() const override;
	MachineSnapshot snapshot() const override;
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) override;

	static const char* stateName(State state);
	static const char* eventName(Event event);
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

	// Under m_mutex: moves along `event` and records the transition to publish.
	// False when the current state has no row for it.
	bool move(Event event, EventSource source, std::vector<MachineTransition>& published);
	void count(RefreshMode mode);
	void counted(RefreshMode mode, const RefreshInfo& info);
	void resetChoice();
	std::string contextLocked() const;

	mutable std::mutex m_mutex;
	std::condition_variable m_decided;
	StateMachine<State, Event> m_machine;

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
