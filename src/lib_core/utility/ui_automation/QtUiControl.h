#ifndef QT_UI_CONTROL_H
#define QT_UI_CONTROL_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Structural UI control: the sender counterpart to QtUiSnapshot. Resolve an
// ElementRef (the ref the snapshot emits per node) back to a live
// QAccessibleInterface, then invoke an accessible action on it. A ref is an
// `objectName` anchor, the widget carrying that name anywhere in the app, with
// `path` (role/name/index steps) down from it; or, without an anchor, a `path`
// from a top-level window. See lib_gui's QtUiAnchor.h. Shared by the
// agent-control InvokeAction and CaptureElement commands.
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

//! Result of an element capture: PNG bytes (empty on failure) + pixel size.
struct CaptureResult
{
	bool ok;
	std::string message;
	std::vector<std::uint8_t> png;
	std::uint32_t width;
	std::uint32_t height;
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
	const std::string& text,
	std::uint64_t expectHash = 0);

//! Resolve `objectName` + `path` and grab the element to a PNG. A QWidget-backed
//! element is grabbed directly; a sub-element (menu item, cell) is grabbed from
//! its window cropped to the element's rect. MUST run on the Qt GUI thread;
//! returns `{false, ...}` when built without Qt accessibility support.
CaptureResult captureElement(
	const std::string& objectName, const std::vector<RefStep>& path, std::uint64_t expectHash = 0);

//! Capture the screen as a user sees it: every visible top-level window
//! composited at its global position (dialogs and popups over the main window)
//! to a PNG. A region of non-zero `regionW` x `regionH` crops the composite, in
//! its own pixels, before `maxWidth` (when non-zero) downscales the result with
//! the aspect kept. On success the message names the composited windows
//! ("windows: 'title' WxH (modal); ...") and ends with " | origin X,Y scale S":
//! frame pixel (px, py) is the global point (X + px / S, Y + py / S). Backs the
//! GetFrame command. MUST run on the Qt GUI thread; needs no accessibility support.
CaptureResult captureScreen(
	std::uint32_t maxWidth = 0,
	std::int32_t regionX = 0,
	std::int32_t regionY = 0,
	std::int32_t regionW = 0,
	std::int32_t regionH = 0);

//! Kind of a synthetic input event (mirrors the schema's InputEventKind).
enum class InputKind
{
	MouseMove,
	MousePress,
	MouseRelease,
	MouseDoubleClick,
	Wheel,
	KeyPress,
	KeyRelease
};

//! One event of a SendInput gesture (see agent_command.fbs, InputEvent). x / y are
//! relative to the target element for a ref target and global for a point target;
//! `centered` puts a mouse event at the element's center, or at the point.
struct InputEvent
{
	InputKind kind = InputKind::MouseMove;
	std::int32_t x = 0;
	std::int32_t y = 0;
	bool centered = false;
	std::int32_t button = 0;	   // 0 none, 1 left, 2 right, 3 middle
	std::uint32_t buttons = 0;	   // held: bit0 left, bit1 right, bit2 middle
	std::uint32_t modifiers = 0;   // bit0 Shift, bit1 Ctrl, bit2 Alt, bit3 Meta
	std::int32_t wheelDelta = 0;   // 120 = one notch up
	std::uint32_t key = 0;		   // Qt::Key
	std::string text;
};

//! Queue a gesture for the element `objectName` + `path` address (a scroll area,
//! like the graph view, receives it on its viewport). The events are posted, not
//! sent, so a press that opens a menu or a dialog cannot block the caller; after
//! the last one `delivered` runs on the GUI thread. A right-button release also
//! posts the context-menu event a real one would cause. On success the message
//! names the receiving widget. MUST run on the Qt GUI thread; needs accessibility
//! support to resolve the ref.
ControlResult sendInput(
	const std::string& objectName,
	const std::vector<RefStep>& path,
	const std::vector<InputEvent>& events,
	std::function<void()> delivered);

//! sendInput to the deepest widget under the global point (x, y): reaches what has
//! no accessible element, like the graph's nodes. Event coordinates are global.
//! MUST run on the Qt GUI thread; needs no accessibility support.
ControlResult sendInputAt(
	std::int32_t x, std::int32_t y, const std::vector<InputEvent>& events, std::function<void()> delivered);

//! Start (`true`) or stop watching for dialogs, for currentDialog(). Callable
//! from any thread: the watch itself is installed on the GUI thread. `changed`, when
//! given, is told what is in front of the app each time that changes (on the GUI
//! thread; "" when a dialog went away).
void watchDialogs(bool on, std::function<void(const std::string&)> changed = {});

//! What is in front of the app, waiting for an answer: the active modal window
//! ("'title' (modal): text"), else the title of the visible in-window dialog
//! (Sourcetrail's window stack, e.g. "Start Indexing"); empty when nothing is.
//! Callable from any thread: it reads what the watch last saw on the GUI thread,
//! which is kept current as windows and dialogs show and hide.
std::string currentDialog();
}	 // namespace utility::qt

#endif	  // QT_UI_CONTROL_H
