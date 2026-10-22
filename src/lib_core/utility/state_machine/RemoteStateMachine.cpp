#include "RemoteStateMachine.h"

#include <algorithm>

const char* eventSourceName(EventSource source)
{
	switch (source)
	{
	case EventSource::Ui:
		return "ui";
	case EventSource::Agent:
		return "agent";
	case EventSource::App:
		break;
	}
	return "app";
}

StateMachineRegistry& StateMachineRegistry::instance()
{
	static StateMachineRegistry registry;
	return registry;
}

void StateMachineRegistry::add(AnyStateMachine machine)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	if (!std::ranges::contains(m_machines, machine.name(), &AnyStateMachine::name))
	{
		m_machines.push_back(std::move(machine));
	}
}

std::optional<AnyStateMachine> StateMachineRegistry::find(const std::string& name) const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	if (const auto it = std::ranges::find(m_machines, name, &AnyStateMachine::name); it != m_machines.end())
	{
		return *it;
	}
	return std::nullopt;
}

std::vector<AnyStateMachine> StateMachineRegistry::all() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return m_machines;
}

std::size_t StateMachineRegistry::addObserver(Observer observer)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	const std::size_t id = m_nextObserverId++;
	m_observers.emplace(id, std::move(observer));
	return id;
}

void StateMachineRegistry::removeObserver(std::size_t id)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	m_observers.erase(id);
}

void StateMachineRegistry::publish(const MachineTransition& transition) const
{
	std::vector<Observer> observers;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& [id, observer]: m_observers)
		{
			observers.push_back(observer);
		}
	}
	for (const Observer& observer: observers)
	{
		observer(transition);
	}
}
