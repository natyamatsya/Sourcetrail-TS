#ifndef MESSAGE_FILTER_H
#define MESSAGE_FILTER_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "MessageQueue.h"


// GM-attached in the module build ([module.unit]/7 -- extern "C++" contents attach to the global
// module): this type appears in the signatures of the classic-forever MessageQueue seam
// (MessageQueue.cpp, stdexec pimpl), and module attachment is part of a class type's mangling,
// so the seam only links if these types mangle identically on both sides. Playbook rule 11.
#ifdef SRCTRL_MODULE_PURVIEW
export extern "C++" {
#endif
class MessageFilter
{
public:
	virtual ~MessageFilter() = default;

	virtual void filter(MessageQueue::MessageBufferType* messageBuffer) = 0;
};
#ifdef SRCTRL_MODULE_PURVIEW
}
#endif

#endif	  // MESSAGE_FILTER_H
