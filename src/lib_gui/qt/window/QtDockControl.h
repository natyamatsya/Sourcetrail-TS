#ifndef QT_DOCK_CONTROL_H
#define QT_DOCK_CONTROL_H

#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QTimer>

#include "DockStateMachine.h"
#include "MessageCause.h"

namespace KDDockWidgets::QtWidgets
{
class DockWidget;
class MainWindow;
}	 // namespace KDDockWidgets::QtWidgets

// Why the window could not do what an agent asked of a dock.
enum class DockApplyError : std::uint8_t
{
	DockGone,
	NotDone
};

constexpr std::string_view to_std_sv(DockApplyError error) noexcept
{
	using enum DockApplyError;
	switch (error)
	{
	case DockGone: return "is no dock of the window any more";
	case NotDone: return "did not change as asked";
	}
	return "could not be changed";
}

struct DockApplyFailure
{
	DockApplyError error{DockApplyError::NotDone};
	std::string dock;
	std::string detail;

	[[nodiscard]] std::string message() const
	{
		return detail.empty() ? std::format("'{}' {}", dock, to_std_sv(error))
							  : std::format("'{}' {} ({})", dock, to_std_sv(error), detail);
	}
};

// The main window's docks for the docks machine (DockStateMachine): arranges them as an agent
// asks, with KDDockWidgets' own calls (those a dock's buttons and drags make, so a person's view
// and the saved layout stay as they would after a person's change), and tells the machine how
// they are arranged, whoever changed it. A person's changes are reported once they settle: not
// in the middle of a drag.
class QtDockControl: public QObject
{
public:
	// `resetLayout`: View > Reset Layout.
	QtDockControl(KDDockWidgets::QtWidgets::MainWindow& window, DockStateMachine& machine, std::function<void()> resetLayout);
	~QtDockControl() override;

	QtDockControl(const QtDockControl&) = delete;
	QtDockControl& operator=(const QtDockControl&) = delete;

	// A dock of the window's; the machine lists them in the order they come.
	void addDock(KDDockWidgets::QtWidgets::DockWidget& dock);

	// The docks as the window has them now. GUI thread.
	DockLayout layout() const;

	// PresentsDocks: does `request` on the GUI thread and reports it to the machine once the
	// layout settled.
	void applyToDocks(const DockRequest& request);

	// Does `request` now, GUI thread: what it asked KDDockWidgets, or why it could not.
	std::expected<void, DockApplyFailure> apply(const DockRequest& request);

	// What `request` led to in `docks`, the layout after it: what it did, or how it fell short.
	static std::expected<std::string, DockApplyFailure> outcome(const DockRequest& request, const DockLayout& docks);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void layoutMayHaveChanged();
	void reportChange();

	KDDockWidgets::QtWidgets::MainWindow& m_window;
	DockStateMachine& m_machine;
	std::function<void()> m_resetLayout;
	std::vector<QPointer<KDDockWidgets::QtWidgets::DockWidget>> m_docks;
	QTimer m_changes;	 // coalesces what a person's change sends
	// The agents' requests the change is on behalf of (their ui_input drags): they settle once it
	// is reported.
	std::vector<CauseWork::Held> m_causes;
};

#endif	  // QT_DOCK_CONTROL_H
