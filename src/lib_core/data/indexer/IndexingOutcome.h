#ifndef INDEXING_OUTCOME_H
#define INDEXING_OUTCOME_H

#include <expected>

#include "utilityExpected.h"

// The terminal outcome of an indexing run, carried by MessageIndexingFinished (ADR-0004:
// std::expected + named scoped-enum error codes instead of exceptions or status ints). The
// invariant it supports: every indexing run reaches EXACTLY ONE terminal event, whatever way it
// ends -- success, a failed/throwing pipeline stage, or termination -- so a headless run can
// always translate the terminal event into an exit code instead of idling forever.
enum class IndexingErrorCode
{
	PipelineFailed,		 // a refresh/indexing stage failed or threw (converted at the boundary)
	PipelineTerminated,	 // the pipeline was terminated before completion (interrupt/shutdown)
};

using IndexingError = utility::ExpectedError<IndexingErrorCode>;
using IndexingOutcome = std::expected<void, IndexingError>;

#endif	  // INDEXING_OUTCOME_H
