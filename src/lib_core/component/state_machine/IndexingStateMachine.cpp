#include "IndexingStateMachine.h"

#include <algorithm>

#include "MachineArgs.h"
#include "MessageIndexingInterrupted.h"
#include "MessageRefresh.h"
#include "TabIds.h"
#include "TabQueues.h"
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

namespace
{
void resetChoice(IndexingMachine::Data& data)
{
	data.countFiles = nullptr;
	data.modes.clear();
	data.infos.clear();
	data.counting.clear();
	data.onStart = nullptr;
	data.onCancel = nullptr;
	data.choicePresenter = {};
}

// `mode` when the open choice offers it; the reason when not.
std::expected<RefreshMode, std::string> offered(const IndexingMachine::Data& data, RefreshMode mode)
{
	if (!std::ranges::contains(data.modes, mode))
	{
		return std::unexpected(
			std::format("mode '{}' is not offered for this refresh", IndexingStateMachine::modeName(mode)));
	}
	return mode;
}
}	 // namespace

std::shared_ptr<IndexingStateMachine> IndexingStateMachine::instance()
{
	static const std::shared_ptr<IndexingStateMachine> machine = std::make_shared<IndexingStateMachine>();
	return machine;
}

void IndexingStateMachine::requested()
{
	auto transaction = m_machine.transaction();
	// Already pending when an agent's `refresh` brought the request here.
	if (transaction.state() == State::Idle)
	{
		transaction.data().outcome.clear();
		transaction.move(Event::Requested);
	}
}

void IndexingStateMachine::requestHandled(const std::string& outcome)
{
	auto transaction = m_machine.transaction();
	if (transaction.state() == State::Pending)
	{
		transaction.data().outcome = outcome;
		transaction.move(Event::End);
	}
}

void IndexingStateMachine::chooseMode(
	std::function<RefreshInfo(RefreshMode)> countFiles,
	const std::vector<RefreshMode>& modes,
	RefreshMode initialMode,
	std::function<void(const RefreshInfo&)> onStart,
	std::function<void()> onCancel,
	IndexingPresenter presenter)
{
	{
		auto transaction = m_machine.transaction();
		Data& data = transaction.data();
		resetChoice(data);
		data.countFiles = std::move(countFiles);
		data.modes = modes;
		data.mode = initialMode;
		data.onStart = std::move(onStart);
		data.onCancel = std::move(onCancel);
		data.choicePresenter = presenter;
		data.counting.insert(initialMode);
		// Every refresh passes requested(); should one ever bypass it, the dialog
		// must still work, so the request is recorded here.
		if (transaction.state() == State::Idle)
		{
			transaction.move(Event::Requested);
		}
		if (!transaction.move(Event::AskMode))
		{
			transaction.defer([state = m_machine.stateName(transaction.state())]() {
				LOG_WARNING(std::format("indexing state machine: the mode choice opened in state {}", state));
			});
		}
	}

	presenter.presentModeChoice(modes, initialMode);
	presenter.presentCounting();
	count(initialMode);
}

void IndexingStateMachine::preparing()
{
	auto transaction = m_machine.transaction();
	transaction.data().progress = {};
	transaction.data().interrupted = false;
	transaction.move(Event::Prepare);
}

void IndexingStateMachine::indexingStarted()
{
	m_machine.transaction().move(Event::Begin);
}

void IndexingStateMachine::progress(
	std::size_t started, std::size_t finished, std::size_t total, const std::string& file)
{
	// Progress changes the context, not the state: no transition to publish (the
	// IndexingProgress event carries it for subscribers).
	auto transaction = m_machine.transaction();
	IndexingMachine::Progress& progress = transaction.data().progress;
	progress.started = started;
	progress.finished = finished;
	progress.total = total;
	if (!file.empty())
	{
		progress.file = file;
	}
}

void IndexingStateMachine::wrappingUp()
{
	auto transaction = m_machine.transaction();
	if (transaction.state() != State::Finishing)
	{
		transaction.move(Event::WrapUp);
	}
}

void IndexingStateMachine::report(
	const IndexingReport& report, IndexingPresenter presenter, std::function<void(DatabasePolicy)> decided)
{
	{
		auto transaction = m_machine.transaction();
		Data& data = transaction.data();
		data.report = report;
		data.reportPresenter = presenter;
		data.decided = std::move(decided);
		if (!transaction.move(Event::Report))
		{
			transaction.defer([state = m_machine.stateName(transaction.state())]() {
				LOG_WARNING(std::format("indexing state machine: the report came in state {}", state));
			});
		}
	}

	if (!presenter.presentReport(report))
	{
		decide(DatabasePolicy::DATABASE_POLICY_KEEP, EventSource::App);
	}
}

DatabasePolicy IndexingStateMachine::report(const IndexingReport& report, IndexingPresenter presenter)
{
	std::mutex mutex;
	std::condition_variable decidedNow;
	std::optional<DatabasePolicy> decision;
	this->report(report, presenter, [&](DatabasePolicy policy) {
		{
			const std::lock_guard<std::mutex> lock(mutex);
			decision = policy;
		}
		decidedNow.notify_all();
	});
	std::unique_lock<std::mutex> lock(mutex);
	decidedNow.wait(lock, [&]() { return decision.has_value(); });
	return *decision;
}

void IndexingStateMachine::ended(const std::string& outcome)
{
	auto transaction = m_machine.transaction();
	Data& data = transaction.data();
	// A run that went through the report keeps the decision as its outcome.
	if (!outcome.empty())
	{
		data.outcome = outcome;
	}
	else if (data.outcome.empty())
	{
		data.outcome = "finished";
	}
	resetChoice(data);
	data.report.reset();
	transaction.move(Event::End);
}

void IndexingStateMachine::detach(IndexingPresenter presenter)
{
	bool reportAbandoned = false;
	{
		auto transaction = m_machine.transaction();
		Data& data = transaction.data();
		if (data.choicePresenter == presenter)
		{
			data.choicePresenter = {};
		}
		if (data.reportPresenter == presenter)
		{
			data.reportPresenter = {};
			reportAbandoned = data.report.has_value();
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
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Refresh))
		{
			return transaction.refuse(Event::Refresh);
		}
		transaction.data().outcome.clear();
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
	IndexingPresenter presenter;
	std::optional<RefreshInfo> known;
	bool needsCount = false;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::SelectMode))
		{
			return transaction.refuse(Event::SelectMode);
		}
		Data& data = transaction.data();
		if (const auto choice = offered(data, mode); !choice)
		{
			return {false, choice.error()};
		}
		data.mode = mode;
		presenter = data.choicePresenter;
		if (const auto it = data.infos.find(mode); it != data.infos.end())
		{
			known = it->second;
		}
		else
		{
			needsCount = data.counting.insert(mode).second;
		}
		transaction.move(Event::SelectMode, source);
	}

	if (presenter)
	{
		// The dialog shows the mode selected, whoever selected it.
		if (known)
		{
			presenter.presentRefreshInfo(*known);
		}
		else
		{
			presenter.presentCounting();
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
	IndexingPresenter presenter;
	std::function<void(const RefreshInfo&)> onStart;
	std::optional<RefreshInfo> info;
	std::function<RefreshInfo(RefreshMode)> countFiles;
	RefreshMode chosen = RefreshMode::NONE;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Start))
		{
			return transaction.refuse(Event::Start);
		}
		Data& data = transaction.data();
		const auto choice = offered(data, mode.value_or(data.mode));
		if (!choice)
		{
			return {false, choice.error()};
		}
		chosen = data.mode = *choice;
		presenter = data.choicePresenter;
		onStart = data.onStart;
		countFiles = data.countFiles;
		if (const auto it = data.infos.find(chosen); it != data.infos.end())
		{
			info = it->second;
		}
		transaction.move(Event::Start, source);
		resetChoice(data);
	}

	// Close the dialog before the pipeline shows its next one.
	if (presenter && source != EventSource::Ui)
	{
		presenter.dismissModeChoice(true);
	}
	// On the app scheduler, as the dialog's start always ran: counting (when the
	// mode was not counted yet) and building the index both belong there.
	execution::post(TabIds::app(), [this, onStart, info, countFiles, chosen]() {
		if (onStart && (info || countFiles))
		{
			onStart(info ? *info : countFiles(chosen));
		}
		requestHandled("nothing to index");
	});
	return {true, std::format("indexing starts with mode '{}'", modeName(chosen))};
}

MachineFireResult IndexingStateMachine::cancel(EventSource source)
{
	IndexingPresenter presenter;
	std::function<void()> onCancel;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Cancel))
		{
			return transaction.refuse(Event::Cancel);
		}
		Data& data = transaction.data();
		presenter = data.choicePresenter;
		onCancel = data.onCancel;
		data.outcome = "canceled";
		transaction.move(Event::Cancel, source);
		resetChoice(data);
	}

	if (presenter && source != EventSource::Ui)
	{
		presenter.dismissModeChoice(false);
	}
	if (onCancel)
	{
		execution::post(TabIds::app(), onCancel);
	}
	return {true, "canceled"};
}

MachineFireResult IndexingStateMachine::interrupt(EventSource source)
{
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Interrupt))
		{
			return transaction.refuse(Event::Interrupt);
		}
		transaction.data().interrupted = true;
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
	IndexingPresenter presenter;
	std::function<void(DatabasePolicy)> decided;
	std::string outcome;
	{
		auto transaction = m_machine.transaction();
		Data& data = transaction.data();
		// A waiting report is what a decision answers (and the decision takes it):
		// whether one waits, not the state, so the report's buttons always release
		// the pipeline.
		if (!data.report)
		{
			return transaction.refuse(event);
		}
		presenter = data.reportPresenter;
		decided = std::move(data.decided);
		data.report.reset();
		data.reportPresenter = {};
		data.outcome = policy == DATABASE_POLICY_DISCARD ? "discarded"
			: policy == DATABASE_POLICY_REFRESH			  ? "reindexing"
			: policy == DATABASE_POLICY_KEEP			  ? "kept"
														  : "closed without a decision";
		outcome = data.outcome;
		if (!transaction.move(event, source))
		{
			transaction.defer([event = m_machine.eventName(event), state = m_machine.stateName(transaction.state())]() {
				LOG_WARNING(std::format("indexing state machine: decided '{}' in state {}", event, state));
			});
		}
	}
	if (presenter && source == EventSource::Agent)
	{
		presenter.dismissReport();
	}
	if (decided)
	{
		decided(policy);
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

	const std::optional<Event> control = m_machine.controlNamed(event);
	if (!control)
	{
		return m_machine.unknownEvent(event);
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
		return m_machine.unknownEvent(event);
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
	const std::function<RefreshInfo(RefreshMode)> countFiles =
		m_machine.read([](const Data& data, State) { return data.countFiles; });
	if (!countFiles)
	{
		return;
	}
	execution::post(TabIds::app(), [this, countFiles, mode]() { counted(mode, countFiles(mode)); });
}

void IndexingStateMachine::counted(RefreshMode mode, const RefreshInfo& info)
{
	IndexingPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		Data& data = transaction.data();
		// A choice that ended meanwhile (started, canceled) has no use for the count.
		if (transaction.state() != State::ChoosingMode || !data.counting.erase(mode))
		{
			return;
		}
		data.infos[mode] = info;
		if (mode == data.mode)
		{
			presenter = data.choicePresenter;
		}
		transaction.move(Event::Counted);
	}

	if (presenter)
	{
		presenter.presentRefreshInfo(info);
	}
}

std::string IndexingMachine::context(State state, const Data& data)
{
	using namespace indexing_machine_json;
	IndexingContext context;
	switch (state)
	{
	case State::ChoosingMode:
		{
			// Not `| std::ranges::to<...>()`: clang cannot instantiate that adaptor from libstdc++ 14.
			const auto modes = data.modes |
				std::views::transform([](RefreshMode mode) { return std::string(IndexingStateMachine::modeName(mode)); });
			context.modes = std::vector<std::string>(modes.begin(), modes.end());
		}
		context.mode = IndexingStateMachine::modeName(data.mode);
		context.counting = data.counting.contains(data.mode);
		if (const auto it = data.infos.find(data.mode); it != data.infos.end())
		{
			context.files_to_index = it->second.filesToIndex.size();
			context.files_to_clear = it->second.filesToClear.size();
		}
		break;
	case State::Preparing:
	case State::Indexing:
	case State::Finishing:
		context.progress = ProgressContext {
			data.progress.started, data.progress.finished, data.progress.total, data.progress.file};
		context.interrupted = data.interrupted;
		break;
	case State::Reporting:
		if (data.report)
		{
			context.report = ReportContext {
				data.report->indexedFileCount,
				data.report->totalIndexedFileCount,
				data.report->completedFileCount,
				data.report->totalFileCount,
				data.report->time,
				data.report->errorCount,
				data.report->fatalCount,
				data.report->interrupted};
		}
		break;
	case State::Idle:
	case State::Pending:
		break;
	}
	if (!data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
