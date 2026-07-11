# AI-Agent Headless UI Exploration for a C++ Desktop App

**Goal:** Enable an AI agent to freely explore and drive the UI of a C++ desktop application in a headless way, as development/testing support.

**Context for the implementing agent:** This is a design brief, not a spec. The recommended path is to build a semantic seam into the app (which you control the source of) rather than relying solely on pixel/vision automation. Concrete tooling and headless mechanics are below. Ask the human for their actual stack (Qt Widgets vs. QML/Qt Quick vs. wxWidgets/Dear ImGui/custom; Windows vs. Linux vs. cross-platform) before committing to specific APIs.

---

## The organizing model: three layers

The ecosystem has converged on a standard taxonomy for how an agent "grabs" a GUI:

1. **Semantic / API seam below the UI** (subcutaneous)
2. **OS accessibility tree** (UI-automation frameworks)
3. **Pixel / vision** (computer-use)

The mature pattern is **hybrid**: use the API seam for structured operations, fall back to the accessibility tree for standard UI elements, and use vision only when the other two fail. This balances performance and coverage at the cost of implementation complexity.

Since you own the source, invest in Layer 1 first — it is the highest-leverage and gives a deterministic, headless-by-construction interface.

---

## Layer 1 — Subcutaneous seam (highest leverage when you own the code)

Structure the app so logic lives in a presenter/view-model (MVP/MVVM, humble-object pattern) and the widget layer is a thin skin over it. Then expose that seam deliberately.

**Options for exposing the seam:**
- A scriptable command bus
- An embedded interpreter (Lua/Python)
- A debug-only JSON-RPC / socket control channel

**Payoff:** The agent queries "what's on screen" as serialized view-model state and issues *named commands* rather than hunting for coordinates. Deterministic, semantic, and headless by construction. Nothing off-the-shelf can match this because only you know the app's domain vocabulary. Doubles as a subcutaneous test harness.

**Trade-off:** More upfront work. Do it anyway.

---

## Layer 2 — Accessibility tree / UI-automation frameworks (the "already systematic" answer)

The OS accessibility APIs are the cross-cutting abstraction the whole ecosystem standardized on:

| Platform | API |
|----------|-----|
| Windows  | UI Automation (UIA) |
| Linux    | AT-SPI2 |
| macOS    | NSAccessibility / AXUIElement |

They expose the widget tree as queryable, actionable structure — roles, names, values, supported actions — which is exactly what an agent wants.

**Agent-ready servers built on this layer:**
- **Windows 365 Agents MCP server** — retrieves the UIA tree, finds elements by text/role/accessible name, returns clickable coordinates for buttons, text boxes, menu items.
- **kwin-mcp** (Linux, KDE Plasma 6 Wayland) — reads the AT-SPI2 tree for structured widget data so the agent interacts without relying solely on vision; launches each app in its own **virtual KWin compositor**, requiring no X11 or physical display. Works with Qt, GTK, and Electron. Suitable for headless CI (GitHub Actions, GitLab CI).
- Generic AT-SPI-based "Linux Desktop" MCP servers exist for reading UI elements + input simulation.

**Qt-specific object-based tooling (deeper than generic accessibility — reaches into the Qt object model):**
- **Squish** — commercial, long-standing. Exposes complete `Q_PROPERTY` properties and slots of all Qt/QML controls to test scripts; no source code or app modification required; cross-platform; scriptable in Python/JS/Perl/Ruby/Tcl. IDE is powerful but clunky to live in day-to-day.
- **Testudos** — newer, more agent/CI-friendly alternative. Python workflow, access to the Qt object tree and properties, headless CI execution, embedded-target support.
- **GammaRay** (KDAB) — runtime introspection instrument. Lets an agent *understand* the live object tree, properties, and signals (inspect, not drive).

**Do this in your app regardless of tool choice:**
- Set stable `objectName`s on widgets.
- Set complete `QAccessible` names/roles.

This is the same investment that helps real screen-reader users and makes every automation layer more robust.

---

## Layer 3 — Vision / computer-use

Screenshot → VLM → click coordinates.

- **Pro:** Most general — works on any app with zero instrumentation.
- **Con:** Most expensive, least deterministic, and still needs a rendered surface.
- **Use for:** canvas-heavy or custom-rendered UI the accessibility tree can't describe.
- In practice, run hybrid: accessibility tree for the structured ~90%, vision only for the rest.

---

## Making it actually headless

| Mechanism | Notes |
|-----------|-------|
| `QT_QPA_PLATFORM=offscreen` | Renders with no display at all. **Caveat:** offscreen widgets often don't receive focus like a foreground window would, which breaks focus-dependent input in some tests. |
| **Xvfb** (X11) | Classic real-virtual-display approach; more robust than offscreen for focus-sensitive flows. |
| Virtual Wayland compositor | `kwin_wayland --virtual`, `cage`, or headless `weston`. |
| `QT_QPA_PLATFORM=vnc` | Offscreen rendering that a human can still peek at over VNC. |

Any of these lets a vision or coordinate-based agent operate in CI with no monitor attached.

---

## Recommended architecture (synthesis)

The pattern crystallizing in 2025–2026: wrap whichever seam you expose — your own command channel, the accessibility tree, or both — in an **MCP server** so any agent (Claude Code, Cursor, etc.) can drive it through typed tools with a screenshot feedback loop. `kwin-mcp` and the Windows 365 server are working templates for this architecture.

**Concrete recommendation for this app:**
1. Build a thin MVVM seam.
2. Expose it, plus complete accessibility metadata.
3. Front it with a small MCP server offering:
   - `get_ui_state`
   - `find_element`
   - `invoke_action`
   - `screenshot`

This gives the agent a fast deterministic path for most work and a vision fallback for the rest — and doubles as the subcutaneous test harness.

---

## Open question to resolve first

Confirm the stack before implementing:
- **UI toolkit:** Qt Widgets · QML/Qt Quick · wxWidgets · Dear ImGui · custom
- **Target OS:** Windows · Linux · macOS · cross-platform

The choice of accessibility API, seam design, and headless mechanism all follow from this.
