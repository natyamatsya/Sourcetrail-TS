#include "IndexingStateMachine.h"

#include <algorithm>

#include <glaze/glaze.hpp>

#include "MessageIndexingInterrupted.h"
#include "MessageRefresh.h"
#include "TabIds.h"
#include "Task.h"
#include "TaskLambda.h"
#include "logging.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace indexing_machine_json
{
struct ProgressContext
{
	std::size_t started = 0;
	std::size_t finished = 0;
	std::size_t total = 0;
	std::string file;
};

struct ReportContext
{
	std::size_t indexed = 0;
	std::size_t total_indexed = 0;
	std::size_t completed = 0;
	std::size_t total = 0;
	float seconds = 0;
	std::size_t errors = 0;
	std::size_t fatal = 0;
	bool interrupted = false;
};

struct IndexingContext
{
	std::optional<std::vector<std::string>> modes;
	std::optional<std::string> mode;
	std::optional<bool> counting;
	std::optional<std::size_t> files_to_index;
	std::optional<std::size_t> files_to_clear;
	std::optional<ProgressContext> progress;
	std::optional<bool> interrupted;
	std::optional<ReportContext> report;
	std::optional<std::string> outcome;
};

struct ModeArgs
{
	std::optional<std::string> mode;
};

struct NoArgs
{
};
}	 // namespace indexing_machine_json

namespace
{
using namespace indexing_machine_json;

using State = IndexingStateMachine::State;
using Event = IndexingStateMachine::Event;

// The events a dialog or an agent may send; the rest come from the pipeline.
bool isControl(Event event)
{
	switch (event)
	{
	case Event::Refresh:
	case Event::SelectMode:
	case Event::Start:
	case Event::Cancel:
	case Event::Interrupt:
	case Event::Keep:
	case Event::Discard:
	case Event::Reindex:
		return true;
	default:
		return false;
	}
}

// Only what the current state is about is filled; glaze leaves out the empty optionals.
template <typename Args>
std::optional<std::string> parseArgs(const std::string& args, Args& parsed)
{
	const std::string text = args.empty() ? std::string("{}") : args;
	if (const auto error = glz::read_json(parsed, text))
	{
		return glz::format_error(error, text);
	}
	return std::nullopt;
}

void publishAll(const std::vector<MachineTransition>& published)
{
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
}

MachineFireResult notAccepted(Event event, State state)
{
	return {
		false,
		std::string("'") + IndexingStateMachine::eventName(event) + "' is not accepted in state '" +
			IndexingStateMachine::stateName(state) + "'"};
}
}	 // namespace

std::shared_ptr<IndexingStateMachine> IndexingStateMachine::instance()
{
	static const std::shared_ptr<IndexingStateMachine> machine = std::make_shared<IndexingStateMachine>();
	return machine;
}

IndexingStateMachine::IndexingStateMachine()
	: m_machine(
		  State::Idle,
		  {
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
		  })
{
}

void IndexingStateMachine::requested()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		// Already pending when an agent's `refresh` brought the request here.
		if (m_machine.state() == State::Idle)
		{
			m_outcome.clear();
			move(Event::Requested, EventSource::App, published);
		}
	}
	publishAll(published);
}

void IndexingStateMachine::requestHandled(const std::string& outcome)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_machine.state() == State::Pending)
		{
			m_outcome = outcome;
			move(Event::End, EventSource::App, published);
		}
	}
	publishAll(published);
}

void IndexingStateMachine::chooseMode(
	std::function<RefreshInfo(RefreshMode)> countFiles,
	const std::vector<RefreshMode>& modes,
	RefreshMode initialMode,
	std::function<void(const RefreshInfo&)> onStart,
	std::function<void()> onCancel,
	IndexingPresenter* presenter)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		resetChoice();
		m_countFiles = std::move(countFiles);
		m_modes = modes;
		m_mode = initialMode;
		m_onStart = std::move(onStart);
		m_onCancel = std::move(onCancel);
		m_choicePresenter = presenter;
		m_counting.insert(initialMode);
		// Every refresh passes requested(); should one ever bypass it, the dialog
		// must still work, so the request is recorded here.
		if (m_machine.state() == State::Idle)
		{
			move(Event::Requested, EventSource::App, published);
		}
		if (!move(Event::AskMode, EventSource::App, published))
		{
			LOG_WARNING(
				std::string("indexing state machine: the mode choice opened in state ") +
				stateName(m_machine.state()));
		}
	}
	publishAll(published);

	presenter->presentModeChoice(modes, initialMode);
	presenter->presentCounting();
	count(initialMode);
}

void IndexingStateMachine::preparing()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_progress = {};
		m_interrupted = false;
		move(Event::Prepare, EventSource::App, published);
	}
	publishAll(published);
}

void IndexingStateMachine::indexingStarted()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		move(Event::Begin, EventSource::App, published);
	}
	publishAll(published);
}

void IndexingStateMachine::progress(
	std::size_t started, std::size_t finished, std::size_t total, const std::string& file)
{
	// Progress changes the context, not the state: no transition to publish (the
	// IndexingProgress event carries it for subscribers).
	const std::lock_guard<std::mutex> lock(m_mutex);
	m_progress.started = started;
	m_progress.finished = finished;
	m_progress.total = total;
	if (!file.empty())
	{
		m_progress.file = file;
	}
}

void IndexingStateMachine::wrappingUp()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_machine.state() != State::Finishing)
		{
			move(Event::WrapUp, EventSource::App, published);
		}
	}
	publishAll(published);
}

DatabasePolicy IndexingStateMachine::report(const IndexingReport& report, IndexingPresenter* presenter)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_report = report;
		m_decision.reset();
		m_reportPresenter = presenter;
		if (!move(Event::Report, EventSource::App, published))
		{
			LOG_WARNING(
				std::string("indexing state machine: the report came in state ") + stateName(m_machine.state()));
		}
	}
	publishAll(published);

	if (!presenter->presentReport(report))
	{
		decide(DatabasePolicy::DATABASE_POLICY_KEEP, EventSource::App);
	}

	std::unique_lock<std::mutex> lock(m_mutex);
	m_decided.wait(lock, [this]() { return m_decision.has_value(); });
	const DatabasePolicy policy = *m_decision;
	m_report.reset();
	m_reportPresenter = nullptr;
	return policy;
}

void IndexingStateMachine::ended(const std::string& outcome)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		// A run that went through the report keeps the decision as its outcome.
		if (!outcome.empty())
		{
			m_outcome = outcome;
		}
		else if (m_outcome.empty())
		{
			m_outcome = "finished";
		}
		resetChoice();
		m_report.reset();
		move(Event::End, EventSource::App, published);
	}
	publishAll(published);
}

void IndexingStateMachine::detach(IndexingPresenter* presenter)
{
	bool reportAbandoned = false;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_choicePresenter == presenter)
		{
			m_choicePresenter = nullptr;
		}
		if (m_reportPresenter == presenter)
		{
			m_reportPresenter = nullptr;
			reportAbandoned = m_machine.state() == State::Reporting && !m_decision;
		}
	}
	if (reportAbandoned)
	{
		decide(DatabasePolicy::DATABASE_POLICY_UNKNOWN, EventSource::App);
	}
}

MachineFireResult IndexingStateMachine::refresh(RefreshMode mode, EventSource source)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::Refresh))
		{
			return notAccepted(Event::Refresh, m_machine.state());
		}
		m_outcome.clear();
		move(Event::Refresh, source, published);
	}
	publishAll(published);

	MessageRefresh message;
	if (mode == RefreshMode::ALL_FILES)
	{
		message.refreshAll();
	}
	message.dispatch();
	return {true, "refresh requested"};
}

MachineFireResult IndexingStateMachine::selectMode(RefreshMode mode, EventSource source)
{
	std::vector<MachineTransition> published;
	IndexingPresenter* presenter = nullptr;
	std::optional<RefreshInfo> known;
	bool needsCount = false;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::SelectMode))
		{
			return notAccepted(Event::SelectMode, m_machine.state());
		}
		if (std::find(m_modes.begin(), m_modes.end(), mode) == m_modes.end())
		{
			return {false, std::string("mode '") + modeName(mode) + "' is not offered for this refresh"};
		}
		m_mode = mode;
		presenter = m_choicePresenter;
		if (auto it = m_infos.find(mode); it != m_infos.end())
		{
			known = it->second;
		}
		else
		{
			needsCount = m_counting.insert(mode).second;
		}
		move(Event::SelectMode, source, published);
	}
	publishAll(published);

	if (presenter != nullptr)
	{
		// The dialog shows the mode selected, whoever selected it.
		if (known)
		{
			presenter->presentRefreshInfo(*known);
		}
		else
		{
			presenter->presentCounting();
		}
	}
	if (needsCount)
	{
		count(mode);
	}
	return {true, std::string("mode '") + modeName(mode) + "' selected"};
}

MachineFireResult IndexingStateMachine::start(std::optional<RefreshMode> mode, EventSource source)
{
	std::vector<MachineTransition> published;
	IndexingPresenter* presenter = nullptr;
	std::function<void(const RefreshInfo&)> onStart;
	std::optional<RefreshInfo> info;
	std::function<RefreshInfo(RefreshMode)> countFiles;
	RefreshMode chosen = RefreshMode::NONE;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::Start))
		{
			return notAccepted(Event::Start, m_machine.state());
		}
		chosen = mode.value_or(m_mode);
		if (std::find(m_modes.begin(), m_modes.end(), chosen) == m_modes.end())
		{
			return {false, std::string("mode '") + modeName(chosen) + "' is not offered for this refresh"};
		}
		m_mode = chosen;
		presenter = m_choicePresenter;
		onStart = m_onStart;
		countFiles = m_countFiles;
		if (auto it = m_infos.find(chosen); it != m_infos.end())
		{
			info = it->second;
		}
		move(Event::Start, source, published);
		resetChoice();
	}
	publishAll(published);

	// Close the dialog before the pipeline shows its next one.
	if (presenter != nullptr && source != EventSource::Ui)
	{
		presenter->dismissModeChoice(true);
	}
	// On the app scheduler, as the dialog's start always ran: counting (when the
	// mode was not counted yet) and building the index both belong there.
	Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([this, onStart, info, countFiles, chosen]() {
		if (onStart && (info || countFiles))
		{
			onStart(info ? *info : countFiles(chosen));
		}
		requestHandled("nothing to index");
	}));
	return {true, std::string("indexing starts with mode '") + modeName(chosen) + "'"};
}

MachineFireResult IndexingStateMachine::cancel(EventSource source)
{
	std::vector<MachineTransition> published;
	IndexingPresenter* presenter = nullptr;
	std::function<void()> onCancel;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::Cancel))
		{
			return notAccepted(Event::Cancel, m_machine.state());
		}
		presenter = m_choicePresenter;
		onCancel = m_onCancel;
		m_outcome = "canceled";
		move(Event::Cancel, source, published);
		resetChoice();
	}
	publishAll(published);

	if (presenter != nullptr && source != EventSource::Ui)
	{
		presenter->dismissModeChoice(false);
	}
	if (onCancel)
	{
		Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([onCancel]() { onCancel(); }));
	}
	return {true, "canceled"};
}

MachineFireResult IndexingStateMachine::interrupt(EventSource source)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::Interrupt))
		{
			return notAccepted(Event::Interrupt, m_machine.state());
		}
		m_interrupted = true;
		move(Event::Interrupt, source, published);
	}
	publishAll(published);

	MessageIndexingInterrupted().dispatch();
	return {true, "interrupting: the files indexed so far are kept for the report"};
}

MachineFireResult IndexingStateMachine::decide(DatabasePolicy policy, EventSource source)
{
	using enum DatabasePolicy;
	const Event event = policy == DATABASE_POLICY_DISCARD ? Event::Discard
		: policy == DATABASE_POLICY_REFRESH				   ? Event::Reindex
														   : Event::Keep;
	std::vector<MachineTransition> published;
	IndexingPresenter* presenter = nullptr;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		// A waiting report is what a decision answers. Should the state ever disagree,
		// the report's buttons must still release the pipeline, so the wait decides.
		if (!m_report || m_decision)
		{
			return notAccepted(event, m_machine.state());
		}
		m_decision = policy;
		presenter = m_reportPresenter;
		m_outcome = policy == DATABASE_POLICY_DISCARD ? "discarded"
			: policy == DATABASE_POLICY_REFRESH		   ? "reindexing"
			: policy == DATABASE_POLICY_KEEP		   ? "kept"
													   : "closed without a decision";
		if (!move(event, source, published))
		{
			LOG_WARNING(
				std::string("indexing state machine: decided '") + eventName(event) + "' in state " +
				stateName(m_machine.state()));
		}
	}
	m_decided.notify_all();
	publishAll(published);

	if (presenter != nullptr && source == EventSource::Agent)
	{
		presenter->dismissReport();
	}
	return {true, m_outcome};
}

std::string IndexingStateMachine::name() const
{
	return "indexing";
}

MachineSnapshot IndexingStateMachine::snapshot() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	MachineSnapshot snapshot;
	snapshot.machine = name();
	snapshot.state = stateName(m_machine.state());
	for (const Event event: m_machine.acceptedEvents())
	{
		if (isControl(event))
		{
			snapshot.events.push_back(eventName(event));
		}
	}
	snapshot.context = contextLocked();
	snapshot.revision = m_machine.revision();
	return snapshot;
}

MachineFireResult IndexingStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	auto parseMode = [&](bool required, std::optional<RefreshMode>& mode) -> std::optional<std::string> {
		ModeArgs parsed;
		if (auto error = parseArgs(args, parsed))
		{
			return event + ": " + *error + R"( (expected {"mode": "updated" | "incomplete" | "all"}))";
		}
		if (!parsed.mode)
		{
			return required ? std::optional<std::string>(event + R"( needs {"mode": "updated" | "incomplete" | "all"})")
							: std::nullopt;
		}
		mode = modeFromName(*parsed.mode);
		if (!mode)
		{
			return "unknown mode '" + *parsed.mode + R"(' (updated, incomplete, all))";
		}
		return std::nullopt;
	};
	auto noArgs = [&]() -> std::optional<std::string> {
		NoArgs parsed;
		if (auto error = parseArgs(args, parsed))
		{
			return event + " takes no arguments: " + *error;
		}
		return std::nullopt;
	};

	std::optional<RefreshMode> mode;
	if (event == "refresh")
	{
		if (auto error = parseMode(false, mode))
		{
			return {false, *error};
		}
		return refresh(mode.value_or(RefreshMode::UPDATED_FILES), source);
	}
	if (event == "select_mode")
	{
		if (auto error = parseMode(true, mode))
		{
			return {false, *error};
		}
		return selectMode(*mode, source);
	}
	if (event == "start")
	{
		if (auto error = parseMode(false, mode))
		{
			return {false, *error};
		}
		return start(mode, source);
	}

	using enum DatabasePolicy;
	const std::map<std::string, std::function<MachineFireResult()>> plain = {
		{"cancel", [&]() { return cancel(source); }},
		{"interrupt", [&]() { return interrupt(source); }},
		{"keep", [&]() { return decide(DATABASE_POLICY_KEEP, source); }},
		{"discard", [&]() { return decide(DATABASE_POLICY_DISCARD, source); }},
		{"reindex", [&]() { return decide(DATABASE_POLICY_REFRESH, source); }},
	};
	if (auto it = plain.find(event); it != plain.end())
	{
		if (auto error = noArgs())
		{
			return {false, *error};
		}
		return it->second();
	}
	return {
		false,
		"unknown event '" + event +
			"' (refresh, select_mode, start, cancel, interrupt, keep, discard, reindex; the others come "
			"from the application)"};
}

const char* IndexingStateMachine::stateName(State state)
{
	switch (state)
	{
	case State::Idle:
		return "idle";
	case State::Pending:
		return "pending";
	case State::ChoosingMode:
		return "choosing_mode";
	case State::Preparing:
		return "preparing";
	case State::Indexing:
		return "indexing";
	case State::Finishing:
		return "finishing";
	case State::Reporting:
		return "reporting";
	}
	return "?";
}

const char* IndexingStateMachine::eventName(Event event)
{
	switch (event)
	{
	case Event::Refresh:
		return "refresh";
	case Event::SelectMode:
		return "select_mode";
	case Event::Start:
		return "start";
	case Event::Cancel:
		return "cancel";
	case Event::Interrupt:
		return "interrupt";
	case Event::Keep:
		return "keep";
	case Event::Discard:
		return "discard";
	case Event::Reindex:
		return "reindex";
	case Event::Requested:
		return "requested";
	case Event::AskMode:
		return "ask_mode";
	case Event::Counted:
		return "counted";
	case Event::Prepare:
		return "prepare";
	case Event::Begin:
		return "begin";
	case Event::WrapUp:
		return "wrap_up";
	case Event::Report:
		return "report";
	case Event::End:
		return "end";
	}
	return "?";
}

const char* IndexingStateMachine::modeName(RefreshMode mode)
{
	switch (mode)
	{
	case RefreshMode::UPDATED_FILES:
		return "updated";
	case RefreshMode::UPDATED_AND_INCOMPLETE_FILES:
		return "incomplete";
	case RefreshMode::ALL_FILES:
		return "all";
	case RefreshMode::NONE:
		break;
	}
	return "none";
}

std::optional<RefreshMode> IndexingStateMachine::modeFromName(const std::string& name)
{
	for (const RefreshMode mode:
		 {RefreshMode::UPDATED_FILES, RefreshMode::UPDATED_AND_INCOMPLETE_FILES, RefreshMode::ALL_FILES})
	{
		if (name == modeName(mode))
		{
			return mode;
		}
	}
	return std::nullopt;
}

bool IndexingStateMachine::move(Event event, EventSource source, std::vector<MachineTransition>& published)
{
	const State from = m_machine.state();
	if (!m_machine.fire(event))
	{
		return false;
	}
	published.push_back(
		{name(),
		 stateName(from),
		 stateName(m_machine.state()),
		 eventName(event),
		 source,
		 contextLocked(),
		 m_machine.revision()});
	return true;
}

// Counts what `mode` would index on the app scheduler, as the dialog always did:
// it walks the source groups and the storage.
void IndexingStateMachine::count(RefreshMode mode)
{
	std::function<RefreshInfo(RefreshMode)> countFiles;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		countFiles = m_countFiles;
	}
	if (!countFiles)
	{
		return;
	}
	Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([this, countFiles, mode]() {
		counted(mode, countFiles(mode));
	}));
}

void IndexingStateMachine::counted(RefreshMode mode, const RefreshInfo& info)
{
	std::vector<MachineTransition> published;
	IndexingPresenter* presenter = nullptr;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		// A choice that ended meanwhile (started, canceled) has no use for the count.
		if (m_machine.state() != State::ChoosingMode || !m_counting.erase(mode))
		{
			return;
		}
		m_infos[mode] = info;
		if (mode == m_mode)
		{
			presenter = m_choicePresenter;
		}
		move(Event::Counted, EventSource::App, published);
	}
	publishAll(published);

	if (presenter != nullptr)
	{
		presenter->presentRefreshInfo(info);
	}
}

void IndexingStateMachine::resetChoice()
{
	m_countFiles = nullptr;
	m_modes.clear();
	m_infos.clear();
	m_counting.clear();
	m_onStart = nullptr;
	m_onCancel = nullptr;
	m_choicePresenter = nullptr;
}

std::string IndexingStateMachine::contextLocked() const
{
	IndexingContext context;
	switch (m_machine.state())
	{
	case State::ChoosingMode:
	{
		std::vector<std::string> modes;
		for (const RefreshMode mode: m_modes)
		{
			modes.emplace_back(modeName(mode));
		}
		context.modes = modes;
		context.mode = modeName(m_mode);
		context.counting = m_counting.contains(m_mode);
		if (auto it = m_infos.find(m_mode); it != m_infos.end())
		{
			context.files_to_index = it->second.filesToIndex.size();
			context.files_to_clear = it->second.filesToClear.size();
		}
		break;
	}
	case State::Preparing:
	case State::Indexing:
	case State::Finishing:
		context.progress =
			ProgressContext {m_progress.started, m_progress.finished, m_progress.total, m_progress.file};
		context.interrupted = m_interrupted;
		break;
	case State::Reporting:
		if (m_report)
		{
			context.report = ReportContext {
				m_report->indexedFileCount,
				m_report->totalIndexedFileCount,
				m_report->completedFileCount,
				m_report->totalFileCount,
				m_report->time,
				m_report->errorCount,
				m_report->fatalCount,
				m_report->interrupted};
		}
		break;
	case State::Idle:
	case State::Pending:
		break;
	}
	if (!m_outcome.empty())
	{
		context.outcome = m_outcome;
	}
	return glz::write_json(context).value_or("{}");
}
