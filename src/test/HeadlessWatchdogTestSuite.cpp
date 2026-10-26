#include "Catch2.hpp"

#include <chrono>

#include "HeadlessWatchdog.h"

namespace
{
using namespace std::chrono_literals;
using Clock = HeadlessWatchdog::Clock;
using IndexingState = IndexingStateMachine::State;
using ProjectState = ProjectStateMachine::State;

// A run's looks, a few seconds apart, as the headless app takes them.
struct Run
{
	HeadlessWatchdog watchdog {HeadlessWatchdog::Limits {}};
	HeadlessWatchdog::Look look;

	Run()
	{
		look.at = Clock::time_point(1h);
		look.lastActivity = look.at;
	}

	std::optional<std::string> after(std::chrono::seconds span)
	{
		std::optional<std::string> reason;
		for (std::chrono::seconds passed = 0s; passed < span && !reason; passed += 5s)
		{
			look.at += 5s;
			reason = watchdog.check(look);
		}
		return reason;
	}
};
}	 // namespace

TEST_CASE("headless watchdog ends a run with nothing left to do, after a grace")
{
	Run run;
	run.look.project = ProjectState::Ready;
	REQUIRE_FALSE(run.after(25s));
	const std::optional<std::string> reason = run.after(10s);
	REQUIRE(reason);
	REQUIRE(reason->find("nothing is left to do") != std::string::npos);
}

TEST_CASE("headless watchdog leaves a run alone that loads, indexes or quits")
{
	Run run;
	run.look.project = ProjectState::Loading;
	REQUIRE_FALSE(run.after(5min));

	run.look.project = ProjectState::Ready;
	run.look.indexing = IndexingState::Preparing;
	REQUIRE_FALSE(run.after(5min));

	run.look.indexing = IndexingState::Idle;
	run.look.app = AppStateMachine::State::Closing;
	REQUIRE_FALSE(run.after(5min));
}

TEST_CASE("headless watchdog ends a run that waits for a person")
{
	Run run;
	run.look.project = ProjectState::Ready;
	run.look.indexing = IndexingState::Reporting;
	REQUIRE_FALSE(run.after(25s));
	const std::optional<std::string> reason = run.after(10s);
	REQUIRE(reason);
	REQUIRE(reason->find("the report") != std::string::npos);
}

TEST_CASE("headless watchdog gives a slow file its time while an indexer runs")
{
	Run run;
	run.look.project = ProjectState::Ready;
	run.look.indexing = IndexingState::Indexing;
	run.look.total = 10;
	run.look.started = 5;
	run.look.finished = 4;
	run.look.liveProcesses = 1;
	run.look.lastActivity = run.look.at;
	REQUIRE_FALSE(run.after(29min));	// one file, still being indexed
	const std::optional<std::string> reason = run.after(2min);
	REQUIRE(reason);
	REQUIRE(reason->find("no progress for") != std::string::npos);
}

TEST_CASE("headless watchdog ends indexing that no indexer process runs any more")
{
	Run run;
	run.look.project = ProjectState::Ready;
	run.look.indexing = IndexingState::Indexing;
	run.look.total = 10;
	run.look.finished = 4;
	run.look.liveProcesses = 0;
	REQUIRE_FALSE(run.after(110s));

	// A file finishing is progress: the limit starts over.
	run.look.finished = 5;
	REQUIRE_FALSE(run.after(110s));
	const std::optional<std::string> reason = run.after(20s);
	REQUIRE(reason);
	REQUIRE(reason->find("no indexer process runs") != std::string::npos);
	REQUIRE(reason->find("5 of 10") != std::string::npos);
}

TEST_CASE("headless watchdog counts a status message as progress")
{
	Run run;
	run.look.project = ProjectState::Ready;
	run.look.indexing = IndexingState::Finishing;
	REQUIRE_FALSE(run.after(25min));
	run.look.lastActivity = run.look.at;
	REQUIRE_FALSE(run.after(25min));
	REQUIRE(run.after(10min));
}
