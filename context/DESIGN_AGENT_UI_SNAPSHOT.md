# Design: structural UI introspection + control (Qt)

A structural read-path for the agent (the *snapshot*), plus its symmetric *sender* that
drives targeted effects on elements addressed in that same tree (see
[Structural control](#structural-control--the-sender-side) below).

The snapshot is a third read-path, alongside the two we have:

| Path | What it gives | Source |
|---|---|---|
| `UiState` | curated *semantic* view (project, graph, code, tabs, …) | `StorageAccess` + controllers |
| frames (Phase C) | pixels (screenshot) | `QWidget::grab()` |
| **snapshot (this doc)** | the **whole widget tree**: roles, properties, geometry, and the **actions the agent can invoke** | Qt meta-object / accessibility |

Qt's meta-object system (moc) gives rich runtime reflection most toolkits lack: walk
the tree, read every property, enumerate invokable methods, serialize to JSON in well
under a hundred lines. This unblocks generic UI navigation ("what's on screen, what can
I click") without hand-curating every element into `UiState`.

## Two sources of structure

### 1. Raw `QObject` / `QMetaObject` (detail)

Every widget is a `QObject`, so the tree is free (`QApplication::topLevelWidgets()` →
`children()`), and `metaObject()` exposes `className()` plus an enumerable property list
— including every inherited `Q_PROPERTY`, so a `QPushButton` surfaces `text`, `enabled`,
`visible`, `checkable`, `geometry`, … with no per-type code. Invokable methods/slots
(`metaObject()->method(i)`) tell the agent *what it can call*.

```cpp
QJsonObject snapshotObject(const QObject* o) {
    QJsonObject node;
    const QMetaObject* mo = o->metaObject();
    node["class"]      = mo->className();
    node["objectName"] = o->objectName();

    QJsonObject props;
    for (int i = 0; i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (!p.isReadable()) continue;
        // Skip heavy/binary values (icons, pixmaps) — summarise, don't serialize.
        if (isHeavyType(p.metaType())) { props[p.name()] = p.typeName(); continue; }
        props[p.name()] = QJsonValue::fromVariant(p.read(o));
    }
    node["properties"] = props;

    if (const auto* w = qobject_cast<const QWidget*>(o)) {
        const QPoint g = w->mapToGlobal(QPoint(0, 0));         // screen coords
        node["screenRect"] = QJsonObject{{"x", g.x()}, {"y", g.y()},
                                         {"w", w->width()}, {"h", w->height()}};
        node["visible"] = w->isVisible();
        node["focus"]   = w->hasFocus();
    }

    QJsonArray kids;
    for (const QObject* c : o->children()) kids.append(snapshotObject(c));
    node["children"] = kids;
    return node;
}
```

**The gotcha:** the `QObject` child tree does **not** contain model/view contents. A
`QTreeView`/`QTableView`/`QListView` is a single node — its rows/cells live in a
`QAbstractItemModel`, not as child `QObject`s. Same for `QGraphicsScene` items
(`QGraphicsItem`, not `QObject`) — which matters here because Sourcetrail's graph is a
`QGraphicsScene`. A naive widget walk says "there is a graph view" but not what's in it.
You'd then query the model (`rowCount`/`index`/`data`) or the scene separately — **or use
the accessibility tree, which already flattens them in.**

### 2. Accessibility interface (semantic — recommended backbone)

`QAccessible::queryAccessibleInterface(obj)` → `QAccessibleInterface*` exposing `role()`,
`text(Name/Value/Description)`, `state()`, `rect()` (screen coords), child navigation, and
— via `actionInterface()` — the supported actions. Better for an agent because it (a)
**normalizes roles** across widget types ("button"/"checkbox"/"row" vs raw class names),
(b) **exposes model/view rows and scene items as accessible children** (solving the
gotcha), and (c) works uniformly for QML/Qt Quick. It is literally the tree screen readers
consume, so "role + name + value + actions + children" is exactly the agent's mental model.

```cpp
QJsonObject snapshotA11y(QAccessibleInterface* iface) {
    QJsonObject node;
    node["role"]        = int(iface->role());   // + name via QMetaEnum where registered
    node["name"]        = iface->text(QAccessible::Name);
    node["value"]       = iface->text(QAccessible::Value);
    node["description"] = iface->text(QAccessible::Description);

    const QRect r = iface->rect();
    node["rect"] = QJsonObject{{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};

    const QAccessible::State s = iface->state();
    node["state"] = QJsonObject{{"focused", bool(s.focused)}, {"checked", bool(s.checked)},
                                {"disabled", bool(s.disabled)}, {"selected", bool(s.selected)},
                                {"invisible", bool(s.invisible)}};

    if (auto* act = iface->actionInterface()) {          // what the agent can invoke
        QJsonArray actions;
        for (const QString& a : act->actionNames()) actions.append(a);
        node["actions"] = actions;                       // act->doAction(name) to invoke
    }

    QJsonArray kids;
    for (int i = 0; i < iface->childCount(); ++i)
        if (QAccessibleInterface* c = iface->child(i)) kids.append(snapshotA11y(c));
    node["children"] = kids;
    return node;
}
```

**Recommended: a hybrid** — the accessibility tree as the semantic backbone, meta-object
properties grafted on for the extra detail (dynamic properties, geometry, invokable
methods) the agent occasionally needs.

## How it plugs into agent-control

- **Where it lives:** a `lib_gui` helper `src/lib_gui/qt/utility/QtUiSnapshot.{h,cpp}`,
  mirroring the Phase-A `QtScreenshot` (Qt code cannot live in `lib`). The
  `AgentControlController` (in `lib`, Qt-free) triggers it through the **`ui()`
  scheduler** — the same GUI-thread hop reserved for the Phase-C frame grab
  (`m_schedulers`), so no new plumbing.
- **Threading (must-do):** introspection runs **only on the GUI thread**. The command
  handler runs on the message thread, so marshal the snapshot call over — `ui()` /
  `execution::qt::onUi`, or `QMetaObject::invokeMethod(w, ..., Qt::BlockingQueuedConnection)`
  — and serialize *there*, returning the finished JSON.
- **Contract additions** (FlatBuffers):
  - `agent_command.fbs`: `GetSnapshot { format: SnapshotFormat = Accessibility; root: string; }`
    (`format` ∈ Accessibility/MetaObject/Hybrid; optional `root` objectName to scope).
  - reply: `SnapshotEnvelope { request_id, format, json }` — a JSON string payload, on a
    dedicated `st.agent.snapshot` route (or reuse `st.agent.state`). Snapshots can be large
    → **chunk per ADR-0002**, exactly like frames.
- **Bridge:** a `get_snapshot(format?, root?)` MCP tool returning the tree; pairs with a
  future `invoke_action(target, action)` (drive `QAccessibleActionInterface::doAction`) so
  the agent can act on *any* element, not just the curated command set.

## Change notifications (avoid blind re-snapshotting)

A snapshot is point-in-time. For a dynamic UI, push change signals instead of polling:
install a `QAccessible::installUpdateHandler(...)` or an event filter, and emit a
lightweight "structure changed" event (on `st.agent.events`) so the agent re-snapshots on
demand. Reuses the existing event-observer pattern.

## Structural control — the sender side

The snapshot is the *reader*; its natural counterpart is a *sender* that causes targeted
effects on specific elements. They are **symmetric**: the snapshot exposes, per node, the
`actions` it supports — the sender invokes one of those actions on a node *addressed in
the same tree*. "Read what you can do, then do it." This is exactly what a UI framework
does internally (route an event/signal to a widget by identity); here an external agent
drives it over the contract.

### Addressing an element (the selector)

A snapshot is point-in-time; the tree may shift before the action lands. So an element is
addressed by a **re-resolvable selector**, never a pointer:

- Each snapshot node carries a `ref`: a path of `{role, name, index}` steps
  (`index` = the nth sibling matching that role+name), optionally **anchored** at the
  nearest ancestor with a unique `objectName`. The app re-walks the live tree and resolves
  it. Role+name paths survive unrelated reordering; the objectName anchor makes them rock-
  solid (this is what the Phase-D `objectName` hygiene pass buys).
- A raw child-index path (`roots[0]/3/1`) is emitted too as a fast exact-match fallback.

### Three mechanisms, mirroring the three snapshot sources

| Snapshot exposes | Sender uses | For |
|---|---|---|
| `actions` (QAccessibleActionInterface) | **`doAction(name)`** | buttons, checkboxes, menu items, list/tree rows, scrollbars, combos — normalized, and works for model/view items + QML |
| invokable methods (QMetaObject) | **`QMetaObject::invokeMethod`** | app-specific slots/`Q_INVOKABLE` with no accessible action — precise, code-level |
| geometry (`rect`) | **synthesized `QMouseEvent`/`QKeyEvent`** posted to the widget at a point | custom-painted widgets, and the **QGraphicsScene graph** (items aren't QObjects) — literal event delivery, the lowest level |

Preference order is top-to-bottom: the **accessible action** is the safe, normalized
default (and the snapshot already told the agent which names are valid); method invocation
and synthesized events are escape hatches for the long tail.

### Command contract (FlatBuffers)

```fbs
table PathStep  { role: string; name: string; index: uint32; }
table ElementRef { object_name: string; path: [PathStep]; }   // anchor + steps

table InvokeAction   { target: ElementRef; action: string; text: string; }  // primary
table InvokeMethod   { target: ElementRef; method: string; args: [string]; } // gated
table SendInputEvent { target: ElementRef; kind: InputKind; x: int32; y: int32; key: string; } // gated
```

Add each to the `Command` union; each replies with a `CommandResult` and, act-and-observe,
a fresh snapshot (or a diff) so the agent sees the effect. Resolution failures are data,
not errors: `ok=false` with `"element not found: <ref>"` or `"action not supported"`.

### App side

A `lib_gui` helper `QtUiControl` (sibling of `QtUiSnapshot`): `resolve(ElementRef)` re-walks
from `QApplication::topLevelWidgets()` to the `QAccessibleInterface` / `QObject` / `QWidget`,
then `invokeAction` / `invokeMethod` / `sendInputEvent`. Runs on the **GUI thread** via the
same `ui()` hop as the snapshot. `AgentControlController` handles the new commands exactly
like the existing ones.

### Two tiers of control (this doesn't replace the semantic commands)

- **Tier 1 — semantic `Command`s** (`ActivateNode`, `LoadProject`, `ActivateTab`, …):
  curated, robust, mapped to `Message<T>` dispatches. The default and preferred path.
- **Tier 2 — structural `InvokeAction`**: generic, accessibility-normalized; the long tail
  of UI the semantic commands don't cover.
- **Tier 3 — `InvokeMethod` / `SendInputEvent`**: powerful escape hatches (arbitrary slot
  invocation, synthetic input) — **capability-gated and logged**; off unless explicitly
  enabled for a trusted session.

Net: the agent reads the tree (snapshot), sees each element's actions, and drives targeted
effects by addressing elements in that same tree — with a graceful fall from normalized
accessible actions down to raw event delivery.

## References / prior art

- **GammaRay** (KDAB) already implements exactly this runtime introspection — remotely,
  with live property editing — so it's a reference implementation to **study**, not a
  blank page. Specific parts worth mining when we implement `QtUiSnapshot`:
  - `core/objecttreemodel` + `probe` — building/maintaining the object tree.
  - `core/util` `VariantHandler` — converting *arbitrary* `QVariant`s to a transportable
    form (handles the `QJsonValue::fromVariant` gap for custom/gadget types — the
    `isHeavyType` filter above is the poor-man's version).
  - its accessibility inspector — the a11y-tree walk, our recommended backbone.
  - model/view + `QGraphicsScene` content extraction — how it introspects
    `QAbstractItemModel` rows and scene items (the gotcha above; relevant to our graph).
  - the remote/streaming protocol — how it moves large trees (informs our ADR-0002
    chunking).
  - **Caveat:** GammaRay is GPL-2.0 (or commercial). Treat it as a *pattern* reference —
    study the approach, write our own ~100-line helper; don't lift code. Study-only, so
    we'd clone it to a scratch/reference location, never vendor it into the repo.
- Qt POC helpers: `QObject::dumpObjectTree()` and `dumpObjectInfo()` print the child
  hierarchy and signal/slot connections to debug output.
- Sibling in-repo: `src/lib_gui/qt/utility/QtScreenshot.{h,cpp}` (Phase A) — the pattern
  for a `lib_gui` helper driven from the agent controller via the schedulers.

## Status

Design only. Fits after the live round-trip is unblocked (thoth-ipc shm-naming fix) and
alongside Phase C (frames), since both use the `ui()` GUI-thread hop and ADR-0002 chunking.
