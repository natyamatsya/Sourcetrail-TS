#include "Catch2.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "IndexingStateMachine.h"
#include "QuestionStateMachine.h"
#include "RemoteStateMachine.h"
#include "StateMachine.h"
#include "TabIds.h"
#include "TaskManager.h"
#include "TaskScheduler.h"

namespace
{
enum class Light
{
	Off,
	On,
	Broken
};

enum class Switch
{
	Press,
	Hit
};

// The transitions one machine published, as "from>to:event(source)".
class Recorder
{
public:
	explicit Recorder(std::string machine): m_machine(std::move(machine))
	{
		m_id = StateMachineRegistry::instance().addObserver([this](const MachineTransition& transition) {
			if (transition.machine == m_machine)
			{
				const std::lock_guard<std::mutex> lock(m_mutex);
				m_path.push_back(
					transition.from + ">" + transition.to + ":" + transition.event + "(" +
					eventSourceName(transition.source) + ")");
			}
		});
	}

	~Recorder()
	{
		StateMachineRegistry::instance().removeObserver(m_id);
	}

	std::vector<std::string> path() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_path;
	}

private:
	std::string m_machine;
	std::size_t m_id = 0;
	mutable std::mutex m_mutex;
	std::vector<std::string> m_path;
};

class FakeQuestionPresenter: public QuestionPresenter
{
public:
	void presentQuestion(std::uint64_t id, const std::string&  /*text*/, const std::vector<std::string>&  /*options*/) override
	{
		const std::lock_guard<std::mutex> lock(mutex);
		presented.push_back(id);
	}

	void dismissQuestion(std::uint64_t id) override
	{
		const std::lock_guard<std::mutex> lock(mutex);
		dismissed.push_back(id);
	}

	std::vector<std::uint64_t> presentedIds()
	{
		const std::lock_guard<std::mutex> lock(mutex);
		return presented;
	}

	std::mutex mutex;
	std::vector<std::uint64_t> presented;
	std::vector<std::uint64_t> dismissed;
};

class FakeIndexingPresenter: public IndexingPresenter
{
public:
	void presentModeChoice(const std::vector<RefreshMode>&  /*modes*/, RefreshMode  /*mode*/) override
	{
		choicePresented = true;
	}
	void presentCounting() override {}
	void presentRefreshInfo(const RefreshInfo& info) override
	{
		const std::lock_guard<std::mutex> lock(mutex);
		shownModes.push_back(info.mode);
	}
	void dismissModeChoice(bool started) override
	{
		choiceDismissed = true;
		choiceDismissedStarted = started;
	}
	bool presentReport(const IndexingReport&  /*report*/) override
	{
		reportPresented = true;
		return answersReports;
	}
	void dismissReport() override
	{
		reportDismissed = true;
	}

	std::mutex mutex;
	std::vector<RefreshMode> shownModes;
	bool answersReports = true;
	std::atomic<bool> choicePresented = false;
	std::atomic<bool> choiceDismissed = false;
	std::atomic<bool> choiceDismissedStarted = false;
	std::atomic<bool> reportPresented = false;
	std::atomic<bool> reportDismissed = false;
};

template <typename Predicate>
bool eventually(Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (predicate())
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return predicate();
}

bool contains(const std::string& text, const std::string& part)
{
	return text.find(part) != std::string::npos;
}

// The mode choice and start run their work on the app scheduler.
class RunningAppScheduler
{
public:
	RunningAppScheduler()
	{
		TaskManager::getScheduler(TabIds::app())->startSchedulerLoopThreaded();
	}
	~RunningAppScheduler()
	{
		TaskManager::getScheduler(TabIds::app())->stopSchedulerLoop();
	}
};
}	 // namespace

TEST_CASE("state machine moves only along its table")
{
	StateMachine<Light, Switch> machine(
		Light::Off,
		{
			{Light::Off, Switch::Press, Light::On},
			{Light::On, Switch::Press, Light::Off},
			{Light::On, Switch::Hit, Light::Broken},
		});

	REQUIRE(machine.acceptedEvents() == std::vector<Switch> {Switch::Press});
	REQUIRE_FALSE(machine.fire(Switch::Hit));
	REQUIRE(machine.state() == Light::Off);
	REQUIRE(machine.revision() == 0);

	REQUIRE(machine.fire(Switch::Press));
	REQUIRE(machine.state() == Light::On);
	REQUIRE(machine.acceptedEvents() == std::vector<Switch> {Switch::Press, Switch::Hit});

	REQUIRE(machine.fire(Switch::Hit));
	REQUIRE(machine.state() == Light::Broken);
	REQUIRE(machine.acceptedEvents().empty());
	REQUIRE(machine.revision() == 2);
}

TEST_CASE("question waits for an answer from remote control")
{
	QuestionStateMachine machine;
	FakeQuestionPresenter presenter;
	Recorder recorder("question");

	std::future<int> answer = std::async(std::launch::async, [&]() {
		return machine.ask("Reindex the project?", {"Reindex", "Cancel"}, &presenter);
	});
	REQUIRE(eventually([&]() { return machine.snapshot().state == "asking"; }));

	const MachineSnapshot snapshot = machine.snapshot();
	REQUIRE(snapshot.events == std::vector<std::string> {"answer"});
	REQUIRE(contains(snapshot.context, R"("text":"Reindex the project?")"));
	REQUIRE(contains(snapshot.context, R"("options":["Reindex","Cancel"])"));

	REQUIRE_FALSE(machine.fire("answer", R"({"label":"Later"})", EventSource::Agent).ok);
	REQUIRE_FALSE(machine.fire("answer", R"({"option":2})", EventSource::Agent).ok);
	REQUIRE_FALSE(machine.fire("answer", R"({"choice":1})", EventSource::Agent).ok);
	REQUIRE_FALSE(machine.fire("ask", "", EventSource::Agent).ok);

	REQUIRE(machine.fire("answer", R"({"label":"Cancel"})", EventSource::Agent).ok);
	REQUIRE(answer.get() == 1);
	REQUIRE(machine.snapshot().state == "idle");
	// The presenter closes what it showed, since someone else answered.
	REQUIRE(presenter.dismissed == presenter.presentedIds());
	REQUIRE(recorder.path() == std::vector<std::string> {"idle>asking:ask(app)", "asking>idle:answer(agent)"});
	REQUIRE_FALSE(machine.fire("answer", R"({"option":0})", EventSource::Agent).ok);
}

TEST_CASE("question asked meanwhile waits behind the open one")
{
	QuestionStateMachine machine;
	FakeQuestionPresenter presenter;

	std::future<int> first = std::async(std::launch::async, [&]() {
		return machine.ask("First?", {"Yes", "No"}, &presenter);
	});
	REQUIRE(eventually([&]() { return presenter.presentedIds().size() == 1; }));
	std::future<int> second = std::async(std::launch::async, [&]() {
		return machine.ask("Second?", {"Yes", "No"}, &presenter);
	});
	REQUIRE(eventually([&]() { return presenter.presentedIds().size() == 2; }));
	REQUIRE(contains(machine.snapshot().context, R"("queued":1)"));

	REQUIRE(machine.fire("answer", R"({"option":0})", EventSource::Agent).ok);
	REQUIRE(first.get() == 0);
	REQUIRE(machine.snapshot().state == "asking");
	REQUIRE(contains(machine.snapshot().context, R"("text":"Second?")"));

	// The user clicks the second box.
	REQUIRE(machine.answer(presenter.presentedIds()[1], 1, EventSource::Ui));
	REQUIRE(second.get() == 1);
	REQUIRE(machine.snapshot().state == "idle");
}

TEST_CASE("question withdrawn answers none")
{
	QuestionStateMachine machine;
	FakeQuestionPresenter presenter;

	std::future<int> answer = std::async(std::launch::async, [&]() {
		return machine.ask("Keep the data?", {"Keep", "Discard"}, &presenter);
	});
	REQUIRE(eventually([&]() { return machine.snapshot().state == "asking"; }));

	machine.withdraw(&presenter);
	REQUIRE(answer.get() == -1);
	REQUIRE(machine.snapshot().state == "idle");
}

TEST_CASE("indexing request nothing came of returns to idle")
{
	IndexingStateMachine machine;
	Recorder recorder("indexing");

	machine.requested();
	REQUIRE(machine.snapshot().state == "pending");
	REQUIRE(machine.snapshot().events.empty());

	machine.requestHandled("no project is loaded");
	const MachineSnapshot snapshot = machine.snapshot();
	REQUIRE(snapshot.state == "idle");
	REQUIRE(snapshot.events == std::vector<std::string> {"refresh"});
	REQUIRE(contains(snapshot.context, R"("outcome":"no project is loaded")"));
	REQUIRE(recorder.path() == std::vector<std::string> {"idle>pending:requested(app)", "pending>idle:end(app)"});
}

TEST_CASE("indexing refuses events its state does not accept")
{
	IndexingStateMachine machine;

	const MachineFireResult start = machine.fire("start", "", EventSource::Agent);
	REQUIRE_FALSE(start.ok);
	REQUIRE(contains(start.message, "'start' is not accepted in state 'idle'"));
	REQUIRE_FALSE(machine.fire("begin", "", EventSource::Agent).ok);
	REQUIRE_FALSE(machine.fire("refresh", R"({"mode":"sideways"})", EventSource::Agent).ok);
	REQUIRE_FALSE(machine.fire("keep", R"({"mode":"all"})", EventSource::Agent).ok);
	REQUIRE(machine.snapshot().state == "idle");
}

TEST_CASE("indexing mode choice counts the files and starts with the chosen mode")
{
	RunningAppScheduler scheduler;
	IndexingStateMachine machine;
	FakeIndexingPresenter presenter;

	std::promise<RefreshMode> started;
	machine.requested();
	machine.chooseMode(
		[](RefreshMode mode) {
			RefreshInfo info;
			info.mode = mode;
			info.filesToIndex.insert(FilePath("a.cpp"));
			if (mode == RefreshMode::ALL_FILES)
			{
				info.filesToIndex.insert(FilePath("b.cpp"));
			}
			return info;
		},
		{RefreshMode::ALL_FILES, RefreshMode::UPDATED_FILES},
		RefreshMode::UPDATED_FILES,
		[&](const RefreshInfo& info) { started.set_value(info.mode); },
		[]() {},
		&presenter);

	REQUIRE(presenter.choicePresented);
	REQUIRE(machine.snapshot().state == "choosing_mode");
	REQUIRE(machine.snapshot().events == std::vector<std::string> {"select_mode", "start", "cancel"});
	REQUIRE(eventually([&]() { return contains(machine.snapshot().context, R"("files_to_index":1)"); }));
	REQUIRE(contains(machine.snapshot().context, R"("modes":["all","updated"])"));

	REQUIRE_FALSE(machine.fire("select_mode", R"({"mode":"incomplete"})", EventSource::Agent).ok);
	REQUIRE(machine.fire("select_mode", R"({"mode":"all"})", EventSource::Agent).ok);
	REQUIRE(eventually([&]() { return contains(machine.snapshot().context, R"("files_to_index":2)"); }));
	{
		const std::lock_guard<std::mutex> lock(presenter.mutex);
		REQUIRE(presenter.shownModes == std::vector<RefreshMode> {RefreshMode::UPDATED_FILES, RefreshMode::ALL_FILES});
	}

	REQUIRE(machine.fire("start", "", EventSource::Agent).ok);
	REQUIRE(started.get_future().get() == RefreshMode::ALL_FILES);
	REQUIRE(presenter.choiceDismissed);
	REQUIRE(presenter.choiceDismissedStarted);
	// The fake pipeline did not prepare anything: the request ends.
	REQUIRE(eventually([&]() { return machine.snapshot().state == "idle"; }));
}

TEST_CASE("indexing mode choice canceled returns to idle")
{
	IndexingStateMachine machine;
	FakeIndexingPresenter presenter;

	// No app scheduler runs here, so the cancel callback it gets stays queued: it
	// must not refer to this test's locals.
	machine.requested();
	machine.chooseMode(
		nullptr, {RefreshMode::ALL_FILES}, RefreshMode::ALL_FILES, [](const RefreshInfo&) {}, []() {}, &presenter);
	REQUIRE(machine.fire("cancel", "", EventSource::Agent).ok);
	REQUIRE(machine.snapshot().state == "idle");
	REQUIRE(presenter.choiceDismissed);
	REQUIRE_FALSE(presenter.choiceDismissedStarted);
	REQUIRE(contains(machine.snapshot().context, R"("outcome":"canceled")"));
}

TEST_CASE("indexing run waits for the decision on its report")
{
	IndexingStateMachine machine;
	FakeIndexingPresenter presenter;
	Recorder recorder("indexing");

	machine.requested();
	machine.preparing();
	machine.indexingStarted();
	machine.progress(3, 2, 10, "a.cpp");
	MachineSnapshot snapshot = machine.snapshot();
	REQUIRE(snapshot.state == "indexing");
	REQUIRE(snapshot.events == std::vector<std::string> {"interrupt"});
	REQUIRE(contains(snapshot.context, R"("progress":{"started":3,"finished":2,"total":10,"file":"a.cpp"})"));

	machine.wrappingUp();
	std::future<DatabasePolicy> decision = std::async(std::launch::async, [&]() {
		IndexingReport report;
		report.indexedFileCount = 10;
		report.totalFileCount = 10;
		return machine.report(report, &presenter);
	});
	REQUIRE(eventually([&]() { return machine.snapshot().state == "reporting"; }));
	snapshot = machine.snapshot();
	REQUIRE(snapshot.events == std::vector<std::string> {"keep", "discard", "reindex"});
	REQUIRE(contains(snapshot.context, R"("indexed":10)"));

	REQUIRE(machine.fire("discard", "", EventSource::Agent).ok);
	REQUIRE(decision.get() == DatabasePolicy::DATABASE_POLICY_DISCARD);
	REQUIRE(presenter.reportDismissed);

	machine.ended("");
	REQUIRE(machine.snapshot().state == "idle");
	REQUIRE(contains(machine.snapshot().context, R"("outcome":"discarded")"));
	REQUIRE(
		recorder.path() ==
		std::vector<std::string> {
			"idle>pending:requested(app)",
			"pending>preparing:prepare(app)",
			"preparing>indexing:begin(app)",
			"indexing>finishing:wrap_up(app)",
			"finishing>reporting:report(app)",
			"reporting>finishing:discard(agent)",
			"finishing>idle:end(app)"});
}

TEST_CASE("indexing report nobody can answer keeps the data")
{
	IndexingStateMachine machine;
	FakeIndexingPresenter presenter;
	presenter.answersReports = false;

	machine.requested();
	machine.preparing();
	machine.indexingStarted();
	REQUIRE(machine.report(IndexingReport(), &presenter) == DatabasePolicy::DATABASE_POLICY_KEEP);
	REQUIRE_FALSE(presenter.reportDismissed);
	machine.ended("");
	REQUIRE(contains(machine.snapshot().context, R"("outcome":"kept")"));
}

TEST_CASE("indexing interrupt moves on to finishing")
{
	IndexingStateMachine machine;

	machine.requested();
	machine.preparing();
	machine.indexingStarted();
	REQUIRE(machine.fire("interrupt", "", EventSource::Agent).ok);
	REQUIRE(machine.snapshot().state == "finishing");
	REQUIRE(contains(machine.snapshot().context, R"("interrupted":true)"));
	REQUIRE_FALSE(machine.fire("interrupt", "", EventSource::Agent).ok);
	machine.ended("");
	REQUIRE(machine.snapshot().state == "idle");
}
