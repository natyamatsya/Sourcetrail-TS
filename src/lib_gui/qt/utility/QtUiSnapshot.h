#ifndef QT_UI_SNAPSHOT_H
#define QT_UI_SNAPSHOT_H

#include <string>

// Structural UI introspection: serialize the live Qt widget tree to JSON for an
// AI agent — class/role, properties, geometry, and the actions each element
// supports. Complements the semantic UiState and the pixel screenshot.
// See context/DESIGN_AGENT_UI_SNAPSHOT.md.
namespace utility::qt
{
enum class SnapshotFormat
{
	//! QAccessible tree: normalized roles ("button"/"row"/…), and it flattens
	//! model/view rows and QGraphicsScene items that the raw widget tree hides.
	Accessibility,
	//! QObject/QMetaObject property walk: raw per-widget detail (every Q_PROPERTY).
	ObjectTree,
};

//! Serialize every top-level widget to a compact JSON string. MUST run on the Qt
//! GUI thread (introspection touches widgets). Falls back to ObjectTree if the Qt
//! build has no accessibility support.
std::string captureUiSnapshot(SnapshotFormat format = SnapshotFormat::Accessibility);

//! One-shot for the `--ui-snapshot` CLI path: after `delayMs` (letting a loaded
//! project render), capture to `jsonPath` and quit. Mirrors
//! scheduleScreenshotAndQuit; call before entering the event loop.
void scheduleSnapshotAndQuit(const std::string& jsonPath, SnapshotFormat format, int delayMs);
}	 // namespace utility::qt

#endif	  // QT_UI_SNAPSHOT_H
