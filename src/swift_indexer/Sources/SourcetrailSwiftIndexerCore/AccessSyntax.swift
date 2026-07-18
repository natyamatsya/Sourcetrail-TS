import Foundation
import SwiftParser
import SwiftSyntax

// SW16 — API-surface metadata. A declaration's access level is purely syntactic,
// so SwiftSyntax supplies it for every file (semantic or syntactic fallback).
// Mapped to Sourcetrail's AccessKind and emitted as a StorageComponentAccess per
// node, enabling a "public API only" graph filter.
//
// Swift's levels map onto Sourcetrail's AccessKinds: open/public → PUBLIC,
// package → PACKAGE, internal (and the implicit default) → DEFAULT,
// fileprivate/private → PRIVATE. `open`/`public` remain indistinguishable (the
// subclassable distinction has no slot). `@available` gating has no representable
// target (a platform is not a node) and is deferred.

// The AccessKind for a declaration's modifier list. No access keyword means the
// implicit default (`internal`) → DEFAULT.
func swiftAccessKind(_ modifiers: DeclModifierListSyntax) -> Int32 {
	for modifier in modifiers {
		switch modifier.name.text {
		case "open", "public": return AccessKind.public_
		case "package": return AccessKind.package
		case "internal": return AccessKind.default_
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
	// SW13: NodeModifier bitmask per declaration name position (actor/async/
	// nonisolated). The index reports actors as classes and carries no async
	// modifier, so SwiftSyntax is the only source.
	private var modifierByPos: [SyntaxPos: Int32] = [:]

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

	// The NodeModifier bitmask for the declaration at `pos` (0 = none).
	func nodeModifiers(at pos: SyntaxPos) -> Int32 {
		modifierByPos[pos] ?? 0
	}

	fileprivate func record(nameToken: TokenSyntax, access: Int32, converter: SourceLocationConverter) {
		let extent = tokenExtent(nameToken, converter)
		byPos[SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))] = access
	}

	fileprivate func recordModifiers(nameToken: TokenSyntax, mask: Int32, converter: SourceLocationConverter) {
		guard mask != 0 else { return }
		let extent = tokenExtent(nameToken, converter)
		modifierByPos[SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))] = mask
	}
}

// The NodeModifier bitmask for a declaration: `nonisolated` from its modifiers,
// `async` from an effect specifier (functions/initializers). `actor` is added by
// the caller from the declaration keyword.
func swiftNodeModifiers(_ modifiers: DeclModifierListSyntax, isAsync: Bool) -> Int32 {
	var mask: Int32 = 0
	if isAsync {
		mask |= NodeModifier.async
	}
	for modifier in modifiers where modifier.name.text == "nonisolated" {
		mask |= NodeModifier.nonisolated
	}
	return mask
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
		record(node.name, node.modifiers)
		map.recordModifiers(nameToken: node.name, mask: NodeModifier.actor, converter: converter)
		return .visitChildren
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
		record(node.name, node.modifiers)
		map.recordModifiers(
			nameToken: node.name,
			mask: swiftNodeModifiers(
				node.modifiers, isAsync: node.signature.effectSpecifiers?.asyncSpecifier != nil),
			converter: converter)
		return .visitChildren
	}
	override func visit(_ node: InitializerDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.initKeyword, node.modifiers)
		map.recordModifiers(
			nameToken: node.initKeyword,
			mask: swiftNodeModifiers(
				node.modifiers, isAsync: node.signature.effectSpecifiers?.asyncSpecifier != nil),
			converter: converter)
		return .visitChildren
	}
	override func visit(_ node: SubscriptDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.subscriptKeyword, node.modifiers)
		map.recordModifiers(
			nameToken: node.subscriptKeyword,
			mask: swiftNodeModifiers(node.modifiers, isAsync: false), converter: converter)
		return .visitChildren
	}
	override func visit(_ node: VariableDeclSyntax) -> SyntaxVisitorContinueKind {
		let mask = swiftNodeModifiers(node.modifiers, isAsync: false)
		for binding in node.bindings {
			if let pattern = binding.pattern.as(IdentifierPatternSyntax.self) {
				record(pattern.identifier, node.modifiers)
				map.recordModifiers(nameToken: pattern.identifier, mask: mask, converter: converter)
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
