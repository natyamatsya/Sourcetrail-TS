#ifndef QT_UI_QUERY_H
#define QT_UI_QUERY_H

#include <cstdint>
#include <string>
#include <vector>

// Server-side element selection: run a JSONPath (RFC 9535, via qt-json-query) over
// the accessibility snapshot and return the matched elements — so an agent filters
// at the source instead of pulling the whole tree over the wire. The queried JSON
// shape matches get_snapshot's output. See DESIGN_AGENT_UI_CONTROL.md (QueryUi).
namespace utility::qt
{
struct QueryResult
{
	bool ok;
	std::string message;
	std::vector<std::uint8_t> snapshot;	// UiSnapshot FlatBuffer of matched nodes (empty on error)
};

//! Evaluate `jsonPath` against the live accessibility tree and return a UiSnapshot
//! whose roots are the matched elements (leaf nodes, each with its ref), tagged
//! with `requestId`. MUST run on the Qt GUI thread.
QueryResult queryUi(const std::string& jsonPath, std::uint64_t requestId);
}	 // namespace utility::qt

#endif	  // QT_UI_QUERY_H
