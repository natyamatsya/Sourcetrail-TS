#include "PreferencesStateMachine.h"

#include <format>
#include <optional>
#include <utility>

#include "MachineArgs.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace preferences_machine_json
{
struct PreferencesContext
{
	std::optional<PreferenceFields> fields;
	std::optional<std::string> applying;
	std::optional<std::string> error;
	std::optional<std::string> outcome;
};

struct SetArgs
{
	PreferenceFields fields;
};
}	 // namespace preferences_machine_json

namespace
{
constexpr PreferencesMachine::Event eventFor(PreferencesRequest::Kind kind)
{
	switch (kind)
	{
		using enum PreferencesRequest::Kind;
	case Open: return PreferencesMachine::Event::Open;
	case Set: return PreferencesMachine::Event::Set;
	case Save: return PreferencesMachine::Event::Save;
	case Cancel: return PreferencesMachine::Event::Cancel;
	}
	std::unreachable();
}
}	 // namespace

std::shared_ptr<PreferencesStateMachine> PreferencesStateMachine::instance()
{
	static const std::shared_ptr<PreferencesStateMachine> machine = std::make_shared<PreferencesStateMachine>();
	return machine;
}

void PreferencesStateMachine::attach(PreferencesPresenter presenter)
{
	auto transaction = m_machine.transaction();
	transaction.data().presenter = presenter;
}

void PreferencesStateMachine::detach(PreferencesPresenter presenter)
{
	auto transaction = m_machine.transaction();
	if (transaction.data().presenter == presenter)
	{
		transaction.data().presenter = {};
	}
}

MachineFireResult PreferencesStateMachine::request(Request request, EventSource source)
{
	const Event event = eventFor(request.kind);

	PreferencesPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(event))
		{
			return transaction.refuse(event);
		}
		PreferencesMachine::Data& data = transaction.data();
		if (!data.presenter)
		{
			return {false, "there is no window to show the preferences in"};
		}

		// What the machine can tell before the dialog is asked.
		for (const auto& [name, value]: request.fields)
		{
			if (!data.fields.contains(name))
			{
				std::string names;
				for (const auto& [field, current]: data.fields)
				{
					names += std::format("{}{}", names.empty() ? "" : ", ", field);
				}
				return {false, std::format("'{}' {} (there are {})", name, to_std_sv(PreferenceFieldError::NotAPreference), names)};
			}
		}

		data.request = std::string(m_machine.eventName(event));
		data.error.clear();
		data.outcome.clear();
		transaction.move(event, source);
		presenter = data.presenter;
	}
	presenter.applyToPreferences(request);
	return {true, std::format("the preferences are on it ({}): until editing or closed", m_machine.eventName(event))};
}

bool PreferencesStateMachine::report(Event event, PreferenceFields fields, const std::string& error)
{
	auto transaction = m_machine.transaction();
	if (!transaction.accepts(event))
	{
		return false;	// a person's step while an agent's request is carried out, or after
	}
	PreferencesMachine::Data& data = transaction.data();
	data.fields = std::move(fields);
	data.error = error;
	data.request.clear();
	transaction.move(event);
	return true;
}

void PreferencesStateMachine::closed(Event event, const std::string& outcome)
{
	auto transaction = m_machine.transaction();
	if (transaction.accepts(event))
	{
		PreferencesMachine::Data& data = transaction.data();
		data.fields.clear();
		data.error.clear();
		data.request.clear();
		data.outcome = outcome;
		transaction.move(event);
	}
}

void PreferencesStateMachine::opened(PreferenceFields fields)
{
	report(Event::Opened, std::move(fields));
}

void PreferencesStateMachine::applied(PreferenceFields fields)
{
	report(Event::Applied, std::move(fields));
}

void PreferencesStateMachine::failed(PreferenceFields fields, const std::string& why)
{
	report(Event::Failed, std::move(fields), why);
}

void PreferencesStateMachine::saved()
{
	closed(Event::Saved, "saved");
}

void PreferencesStateMachine::canceled()
{
	closed(Event::Canceled, "canceled");
}

MachineFireResult PreferencesStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	namespace json = preferences_machine_json;
	using Kind = Request::Kind;

	const std::optional<Event> control = m_machine.controlNamed(event);
	if (!control)
	{
		return m_machine.unknownEvent(event);
	}
	Request request;
	switch (*control)
	{
	case Event::Open:
	case Event::Save:
	case Event::Cancel:
	{
		if (const auto parsed = parseArgs<machine_args::None>(event, args); !parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = *control == Event::Open ? Kind::Open : *control == Event::Save ? Kind::Save : Kind::Cancel;
		break;
	}
	case Event::Set:
	{
		const auto parsed =
			parseArgs<json::SetArgs>(event, args, R"({"fields": {"font_size": "14", "animations": true, ...}})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		if (parsed->fields.empty())
		{
			return {false, "set needs fields"};
		}
		request.kind = Kind::Set;
		request.fields = parsed->fields;
		break;
	}
	default:
		return m_machine.unknownEvent(event);
	}
	return this->request(std::move(request), source);
}

std::string PreferencesMachine::context(State state, const Data& data)
{
	namespace json = preferences_machine_json;
	json::PreferencesContext context;
	if (state != State::Closed)
	{
		context.fields = data.fields;
	}
	if (state == State::Applying && !data.request.empty())
	{
		context.applying = data.request;
	}
	if (!data.error.empty())
	{
		context.error = data.error;
	}
	if (state == State::Closed && !data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
