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

void StateMachineRegistry::add(std::shared_ptr<RemoteStateMachine> machine)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	const std::string name = machine->name();
	const bool known = std::any_of(m_machines.begin(), m_machines.end(), [&](const auto& m) {
		return m->name() == name;
	});
	if (!known)
	{
		m_machines.push_back(std::move(machine));
	}
}

std::shared_ptr<RemoteStateMachine> StateMachineRegistry::find(const std::string& name) const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	for (const std::shared_ptr<RemoteStateMachine>& machine: m_machines)
	{
		if (machine->name() == name)
		{
			return machine;
		}
	}
	return nullptr;
}

std::vector<std::shared_ptr<RemoteStateMachine>> StateMachineRegistry::all() const
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
