#include "MessageCause.h"

namespace
{
thread_local std::uint64_t t_cause = 0;
}	 // namespace

CauseScope::CauseScope(std::uint64_t cause): m_previous(t_cause)
{
	t_cause = cause;
}

CauseScope::~CauseScope()
{
	t_cause = m_previous;
}

std::uint64_t currentCause()
{
	return t_cause;
}
