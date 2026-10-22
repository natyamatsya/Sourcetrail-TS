#include "IndexingStateMachine.h"

#include <algorithm>

#include "MachineArgs.h"
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

// Only what the current state is about is filled; glaze leaves out the empty optionals.
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
}	 // namespace indexing_machine_json

std::shared_ptr<IndexingStateMachine> IndexingStateMachine::instance()
{
	static const std::shared_ptr<IndexingStateMachine> machine = std::make_shared<IndexingStateMachine>();
	return machine;
}

void IndexingStateMachine::requested()
{
	Transaction transaction(*this);
	// Already pending when an agent's `refresh` brought the request here.
	if (transaction.state() == State::Idle)
	{
		m_outcome.clear();
		transaction.move(Event::Requested);
	}
}

void IndexingStateMachine::requestHandled(const std::string& outcome)
{
	Transaction transaction(*this);
	if (transaction.state() == State::Pending)
	{
		m_outcome = outcome;
		transaction.move(Event::End);
	}
}

void IndexingStateMachine::chooseMode(
	std::function<RefreshInfo(RefreshMode)> countFiles,
	const std::vector<RefreshMode>& modes,
	RefreshMode initialMode,
	std::function<void(const RefreshInfo&)> onStart,
	std::function<void()> onCancel,
	IndexingPresenter* presenter)
{
	{
		Transaction transaction(*this);
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
		if (transaction.state() == State::Idle)
		{
			transaction.move(Event::Requested);
		}
		if (!transaction.move(Event::AskMode))
		{
			LOG_WARNING(std::format(
				"indexing state machine: the mode choice opened in state {}", stateName(transaction.state())));
		}
	}

	presenter->presentModeChoice(modes, initialMode);
	presenter->presentCounting();
	count(initialMode);
}

void IndexingStateMachine::preparing()
{
	Transaction transaction(*this);
	m_progress = {};
	m_interrupted = false;
	transaction.move(Event::Prepare);
}

void IndexingStateMachine::indexingStarted()
{
	Transaction transaction(*this);
	transaction.move(Event::Begin);
}

void IndexingStateMachine::progress(
	std::size_t started, std::size_t finished, std::size_t total, const std::string& file)
{
	// Progress changes the context, not the state: no transition to publish (the
	// IndexingProgress event carries it for subscribers).
	const auto lock = this->lock();
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
	Transaction transaction(*this);
	if (transaction.state() != State::Finishing)
	{
		transaction.move(Event::WrapUp);
	}
}

DatabasePolicy IndexingStateMachine::report(const IndexingReport& report, IndexingPresenter* presenter)
{
	{
		Transaction transaction(*this);
		m_report = report;
		m_decision.reset();
		m_reportPresenter = presenter;
		if (!transaction.move(Event::Report))
		{
			LOG_WARNING(std::format(
				"indexing state machine: the report came in state {}", stateName(transaction.state())));
		}
	}

	if (!presenter->presentReport(report))
	{
		decide(DatabasePolicy::DATABASE_POLICY_KEEP, EventSource::App);
	}

	Transaction transaction(*this);
	m_decided.wait(transaction.lock(), [this]() { return m_decision.has_value(); });
	const DatabasePolicy policy = *m_decision;
	m_report.reset();
	m_reportPresenter = nullptr;
	return policy;
}

void IndexingStateMachine::ended(const std::string& outcome)
{
	Transaction transaction(*this);
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
	transaction.move(Event::End);
}

void IndexingStateMachine::detach(IndexingPresenter* presenter)
{
	bool reportAbandoned = false;
	{
		const auto lock = this->lock();
		if (m_choicePresenter == presenter)
		{
			m_choicePresenter = nullptr;
		}
		if (m_reportPresenter == presenter)
		{
			m_reportPresenter = nullptr;
			reportAbandoned = m_report && !m_decision;
		}
	}
	if (reportAbandoned)
	{
		decide(DatabasePolicy::DATABASE_POLICY_UNKNOWN, EventSource::App);
	}
}

MachineFireResult IndexingStateMachine::refresh(RefreshMode mode, EventSource source)
{
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::Refresh))
		{
			return transaction.refuse(Event::Refresh);
		}
		m_outcome.clear();
		transaction.move(Event::Refresh, source);
	}

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
	IndexingPresenter* presenter = nullptr;
	std::optional<RefreshInfo> known;
	bool needsCount = false;
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::SelectMode))
		{
			return transaction.refuse(Event::SelectMode);
		}
		if (const auto choice = offered(mode); !choice)
		{
			return {false, choice.error()};
		}
		m_mode = mode;
		presenter = m_choicePresenter;
		if (const auto it = m_infos.find(mode); it != m_infos.end())
		{
			known = it->second;
		}
		else
		{
			needsCount = m_counting.insert(mode).second;
		}
		transaction.move(Event::SelectMode, source);
	}

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
	return {true, std::format("mode '{}' selected", modeName(mode))};
}

MachineFireResult IndexingStateMachine::start(std::optional<RefreshMode> mode, EventSource source)
{
	IndexingPresenter* presenter = nullptr;
	std::function<void(const RefreshInfo&)> onStart;
	std::optional<RefreshInfo> info;
	std::function<RefreshInfo(RefreshMode)> countFiles;
	RefreshMode chosen = RefreshMode::NONE;
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::Start))
		{
			return transaction.refuse(Event::Start);
		}
		const auto choice = offered(mode.value_or(m_mode));
		if (!choice)
		{
			return {false, choice.error()};
		}
		chosen = m_mode = *choice;
		presenter = m_choicePresenter;
		onStart = m_onStart;
		countFiles = m_countFiles;
		if (const auto it = m_infos.find(chosen); it != m_infos.end())
		{
			info = it->second;
		}
		transaction.move(Event::Start, source);
		resetChoice();
	}

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
	return {true, std::format("indexing starts with mode '{}'", modeName(chosen))};
}

MachineFireResult IndexingStateMachine::cancel(EventSource source)
{
	IndexingPresenter* presenter = nullptr;
	std::function<void()> onCancel;
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::Cancel))
		{
			return transaction.refuse(Event::Cancel);
		}
		presenter = m_choicePresenter;
		onCancel = m_onCancel;
		m_outcome = "canceled";
		transaction.move(Event::Cancel, source);
		resetChoice();
	}

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
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::Interrupt))
		{
			return transaction.refuse(Event::Interrupt);
		}
		m_interrupted = true;
		transaction.move(Event::Interrupt, source);
	}

	MessageIndexingInterrupted().dispatch();
	return {true, "interrupting: the files indexed so far are kept for the report"};
}

MachineFireResult IndexingStateMachine::decide(DatabasePolicy policy, EventSource source)
{
	using enum DatabasePolicy;
	const Event event = policy == DATABASE_POLICY_DISCARD ? Event::Discard
		: policy == DATABASE_POLICY_REFRESH				   ? Event::Reindex
														   : Event::Keep;
	IndexingPresenter* presenter = nullptr;
	std::string outcome;
	{
		Transaction transaction(*this);
		// A waiting report is what a decision answers. Should the state ever disagree,
		// the report's buttons must still release the pipeline, so the wait decides.
		if (!m_report || m_decision)
		{
			return transaction.refuse(event);
		}
		m_decision = policy;
		presenter = m_reportPresenter;
		m_outcome = policy == DATABASE_POLICY_DISCARD ? "discarded"
			: policy == DATABASE_POLICY_REFRESH		   ? "reindexing"
			: policy == DATABASE_POLICY_KEEP		   ? "kept"
													   : "closed without a decision";
		outcome = m_outcome;
		if (!transaction.move(event, source))
		{
			LOG_WARNING(std::format(
				"indexing state machine: decided '{}' in state {}", eventName(event), stateName(transaction.state())));
		}
	}
	m_decided.notify_all();

	if (presenter != nullptr && source == EventSource::Agent)
	{
		presenter->dismissReport();
	}
	return {true, outcome};
}

MachineFireResult IndexingStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	// {"mode": "..."}, which select_mode needs and refresh and start may have.
	const auto parseMode = [&](bool required) -> std::expected<std::optional<RefreshMode>, std::string> {
		return parseArgs<indexing_machine_json::ModeArgs>(event, args, R"({"mode": "updated" | "incomplete" | "all"})")
			.and_then([&](const indexing_machine_json::ModeArgs& parsed)
						  -> std::expected<std::optional<RefreshMode>, std::string> {
				if (!parsed.mode)
				{
					if (required)
					{
						return std::unexpected(event + R"( needs {"mode": "updated" | "incomplete" | "all"})");
					}
					return std::optional<RefreshMode>();
				}
				if (const std::optional<RefreshMode> mode = modeFromName(*parsed.mode))
				{
					return mode;
				}
				return std::unexpected(std::format("unknown mode '{}' (updated, incomplete, all)", *parsed.mode));
			});
	};

	const std::optional<Event> control = controlNamed(event);
	if (!control)
	{
		return unknownEvent(event);
	}
	switch (*control)
	{
	case Event::Refresh:
	case Event::SelectMode:
	case Event::Start:
	{
		const auto mode = parseMode(*control == Event::SelectMode);
		if (!mode)
		{
			return {false, mode.error()};
		}
		if (*control == Event::Refresh)
		{
			return refresh(mode->value_or(RefreshMode::UPDATED_FILES), source);
		}
		return *control == Event::SelectMode ? selectMode(**mode, source) : start(*mode, source);
	}
	default:
		break;
	}

	if (const auto none = parseArgs<machine_args::None>(event, args); !none)
	{
		return {false, none.error()};
	}
	using enum DatabasePolicy;
	switch (*control)
	{
	case Event::Cancel:
		return cancel(source);
	case Event::Interrupt:
		return interrupt(source);
	case Event::Keep:
		return decide(DATABASE_POLICY_KEEP, source);
	case Event::Discard:
		return decide(DATABASE_POLICY_DISCARD, source);
	case Event::Reindex:
		return decide(DATABASE_POLICY_REFRESH, source);
	default:
		return unknownEvent(event);
	}
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

// Counts what `mode` would index on the app scheduler, as the dialog always did:
// it walks the source groups and the storage.
void IndexingStateMachine::count(RefreshMode mode)
{
	std::function<RefreshInfo(RefreshMode)> countFiles;
	{
		const auto lock = this->lock();
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
	IndexingPresenter* presenter = nullptr;
	{
		Transaction transaction(*this);
		// A choice that ended meanwhile (started, canceled) has no use for the count.
		if (transaction.state() != State::ChoosingMode || !m_counting.erase(mode))
		{
			return;
		}
		m_infos[mode] = info;
		if (mode == m_mode)
		{
			presenter = m_choicePresenter;
		}
		transaction.move(Event::Counted);
	}

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

std::expected<RefreshMode, std::string> IndexingStateMachine::offered(RefreshMode mode) const
{
	if (!std::ranges::contains(m_modes, mode))
	{
		return std::unexpected(std::format("mode '{}' is not offered for this refresh", modeName(mode)));
	}
	return mode;
}

std::string IndexingStateMachine::contextLocked() const
{
	using namespace indexing_machine_json;
	IndexingContext context;
	switch (stateLocked())
	{
	case State::ChoosingMode:
		context.modes = m_modes |
			std::views::transform([](RefreshMode mode) { return std::string(modeName(mode)); }) |
			std::ranges::to<std::vector<std::string>>();
		context.mode = modeName(m_mode);
		context.counting = m_counting.contains(m_mode);
		if (const auto it = m_infos.find(m_mode); it != m_infos.end())
		{
			context.files_to_index = it->second.filesToIndex.size();
			context.files_to_clear = it->second.filesToClear.size();
		}
		break;
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
