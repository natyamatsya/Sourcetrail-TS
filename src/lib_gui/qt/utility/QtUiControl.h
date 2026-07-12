#ifndef QT_UI_CONTROL_H
#define QT_UI_CONTROL_H

#include <cstdint>
#include <string>
#include <vector>

// Structural UI control: the sender counterpart to QtUiSnapshot. Resolve an
// ElementRef (the same role/name/index selector the snapshot emits per node) back
// to a live QAccessibleInterface, then invoke an accessible action on it. Shared
// by the agent-control InvokeAction and CaptureElement commands.
// See context/DESIGN_AGENT_UI_CONTROL.md (Structural control).
namespace utility::qt
{
//! One resolved selector step: nth element among siblings sharing (role, name).
struct RefStep
{
	std::string role;
	std::string name;
	std::uint32_t index;
};

//! Result of a control attempt: `ok` plus a human message on failure. Resolution
//! failures are data, not exceptions (message: "element not found: ...").
struct ControlResult
{
	bool ok;
	std::string message;
};

//! Invoke `action` (QAccessibleActionInterface::doAction) on the element
//! addressed by `objectName` + `path`. When `text` is non-empty it sets the
//! element's editable text/value (line edits, combos). At least one of
//! `action`/`text` should be set. MUST run on the Qt GUI thread; returns
//! `{false, ...}` when built without Qt accessibility support.
ControlResult invokeAction(
	const std::string& objectName,
	const std::vector<RefStep>& path,
	const std::string& action,
	const std::string& text);
}	 // namespace utility::qt

#endif	  // QT_UI_CONTROL_H
