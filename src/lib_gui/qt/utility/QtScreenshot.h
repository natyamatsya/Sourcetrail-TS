#ifndef QT_SCREENSHOT_H
#define QT_SCREENSHOT_H

#include <string>

// Headless screenshot support for agent-driven / CI use (see
// context/DESIGN_AGENT_UI_CONTROL.md, Phase A). Intended to run under
// QT_QPA_PLATFORM=offscreen: no display required, yet the real GUI renders.
namespace utility::qt
{
// Schedule a one-shot capture of the application's main window to `pngPath`
// after `delayMs` (giving a just-loaded project time to render), then quit the
// application. Must be called after the QApplication and main window exist and
// before entering the event loop.
void scheduleScreenshotAndQuit(const std::string& pngPath, int delayMs);
}	 // namespace utility::qt

#endif	  // QT_SCREENSHOT_H
