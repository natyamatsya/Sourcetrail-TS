#ifndef MESSAGE_INDEXING_FINISHED_H
#define MESSAGE_INDEXING_FINISHED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

// Header-only outcome type (std::expected + enum), classic-clean: textually attached.
#include "IndexingOutcome.h"
#ifndef SRCTRL_MODULE_PURVIEW
#include <utility>

#endif

// The single terminal event of an indexing run. Dispatched exactly once per run: by the final
// pipeline stage on success, or by the pipeline's owner (Project's exception boundary / the
// root TaskFinally decorator) on failure or termination. Headless mode maps `outcome` to the
// process exit code.
SRCTRL_EXPORT class MessageIndexingFinished: public Message<MessageIndexingFinished>
{
public:
	static const std::string getStaticType()
	{
		return "MessageIndexingFinished";
	}

	MessageIndexingFinished() = default;

	explicit MessageIndexingFinished(IndexingOutcome outcome): outcome(std::move(outcome)) {}

	IndexingOutcome outcome;	// default-constructed = success
};

#endif	  // MESSAGE_INDEXING_FINISHED_H
