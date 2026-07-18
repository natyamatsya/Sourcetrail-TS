import Foundation
import SwiftParser
import SwiftSyntax

// SW16 — API-surface metadata. A declaration's access level is purely syntactic,
// so SwiftSyntax supplies it for every file (semantic or syntactic fallback).
// Mapped to Sourcetrail's AccessKind and emitted as a StorageComponentAccess per
// node, enabling a "public API only" graph filter.
//
// Swift's six levels collapse onto Sourcetrail's four: open/public → PUBLIC,
// package/internal (and the implicit default) → DEFAULT, fileprivate/private →
// PRIVATE. `package` has no dedicated slot and `open`/`public` are
// indistinguishable — documented limitations. `@available` gating has no
// representable target (a platform is not a node) and is deferred.

// The AccessKind for a declaration's modifier list. No access keyword means the
// implicit default (`internal`) → DEFAULT.
func swiftAccessKind(_ modifiers: DeclModifierListSyntax) -> Int32 {
	for modifier in modifiers {
		switch modifier.name.text {
		case "open", "public": return AccessKind.public_
		case "package", "internal": return AccessKind.default_
		case "fileprivate", "private": return AccessKind.private_
		default: continue
		}
	}
	return AccessKind.default_
}

// Name-token position → AccessKind, for the semantic pass to join against the
// store's definition occurrences (same keying as DeclScopeMap).
final class AccessMap {
	private var byPos: [SyntaxPos: Int32] = [:]

	static func build(path: String) -> AccessMap {
		let map = AccessMap()
		guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
			return map
		}
		let tree = Parser.parse(source: source)
		let converter = SourceLocationConverter(fileName: path, tree: tree)
		AccessVisitor(map: map, converter: converter).walk(tree)
		return map
	}

	func access(at pos: SyntaxPos) -> Int32? {
		byPos[pos]
	}

	fileprivate func record(nameToken: TokenSyntax, access: Int32, converter: SourceLocationConverter) {
		let extent = tokenExtent(nameToken, converter)
		byPos[SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))] = access
	}
}

// Records each declaration's access at the same name token the index reports its
// definition at (mirrors DeclScopeVisitor's token selection).
private final class AccessVisitor: SyntaxVisitor {
	private let map: AccessMap
	private let converter: SourceLocationConverter

	init(map: AccessMap, converter: SourceLocationConverter) {
		self.map = map
		self.converter = converter
		super.init(viewMode: .sourceAccurate)
	}

	private func record(_ nameToken: TokenSyntax, _ modifiers: DeclModifierListSyntax) {
		map.record(nameToken: nameToken, access: swiftAccessKind(modifiers), converter: converter)
	}

	override func visit(_ node: StructDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .visitChildren
	}
	override func visit(_ node: ClassDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .visitChildren
	}
	override func visit(_ node: ActorDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .visitChildren
	}
	override func visit(_ node: EnumDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .visitChildren
	}
	override func visit(_ node: ProtocolDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .visitChildren
	}
	override func visit(_ node: TypeAliasDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .skipChildren
	}
	override func visit(_ node: FunctionDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers); return .visitChildren
	}
	override func visit(_ node: InitializerDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.initKeyword, node.modifiers); return .visitChildren
	}
	override func visit(_ node: SubscriptDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.subscriptKeyword, node.modifiers); return .visitChildren
	}
	override func visit(_ node: VariableDeclSyntax) -> SyntaxVisitorContinueKind {
		for binding in node.bindings {
			if let pattern = binding.pattern.as(IdentifierPatternSyntax.self) {
				record(pattern.identifier, node.modifiers)
			}
		}
		return .skipChildren
	}
	override func visit(_ node: EnumCaseDeclSyntax) -> SyntaxVisitorContinueKind {
		// A case takes the enum's access; it carries no modifiers of its own.
		for element in node.elements {
			map.record(nameToken: element.name, access: AccessKind.default_, converter: converter)
		}
		return .skipChildren
	}
}
