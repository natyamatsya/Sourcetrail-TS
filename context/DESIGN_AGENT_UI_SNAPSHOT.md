# Design: structural UI snapshot (Qt introspection)

A third read-path for the agent, alongside the two we have:

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
