#ifndef QT_UI_SNAPSHOT_H
#define QT_UI_SNAPSHOT_H

#include <cstdint>
#include <string>
#include <vector>

// Structural UI introspection: serialize the live Qt widget tree to a typed
// FlatBuffer (agent_snapshot.fbs `UiSnapshot`) for an AI agent — class/role, name,
// geometry, state, and the actions each element supports, with each node's `ref`
// being the same ElementRef the structural-control commands consume. Complements
// the semantic UiState and the pixel screenshot.
// See context/DESIGN_AGENT_UI_SNAPSHOT.md.
namespace utility::qt
{
enum class SnapshotFormat
{
	//! QAccessible tree: normalized roles ("button"/"row"/…); flattens model/view
	//! rows and QGraphicsScene items that the raw widget tree hides.
	Accessibility,
	//! QObject/QMetaObject walk: class + a FlexBuffer bag of every Q_PROPERTY.
	ObjectTree,
};

//! Serialize every top-level widget to a `UiSnapshot` FlatBuffer, tagged with
//! `requestId` for reply correlation (0 for the CLI dump). MUST run on the Qt GUI
//! thread. Empty when built without SOURCETRAIL_AGENT_CONTROL.
std::vector<std::uint8_t> captureUiSnapshot(
	SnapshotFormat format = SnapshotFormat::Accessibility, std::uint64_t requestId = 0);

//! One-shot for the `--ui-snapshot` CLI path: after `delayMs` (letting a loaded
//! project render), write the FlatBuffer to `path` and quit.
void scheduleSnapshotAndQuit(const std::string& path, SnapshotFormat format, int delayMs);
}	 // namespace utility::qt

#endif	  // QT_UI_SNAPSHOT_H
