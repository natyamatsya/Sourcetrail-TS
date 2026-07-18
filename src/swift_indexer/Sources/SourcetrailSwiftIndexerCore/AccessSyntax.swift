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
	// @available specification text per declaration name position (Axis-3 metadata).
	private var availabilityByPos: [SyntaxPos: String] = [:]
	// Deprecation message per declaration name position (only when non-empty; the
	// boolean itself rides the NODE_MODIFIER_DEPRECATED bit in modifierByPos).
	private var deprecationByPos: [SyntaxPos: String] = [:]

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

	// The @available spec text for the declaration at `pos` (nil = none).
	func availability(at pos: SyntaxPos) -> String? {
		availabilityByPos[pos]
	}

	// The deprecation message for the declaration at `pos` (nil = none/empty).
	func deprecationMessage(at pos: SyntaxPos) -> String? {
		deprecationByPos[pos]
	}

	fileprivate func record(nameToken: TokenSyntax, access: Int32, converter: SourceLocationConverter) {
		let extent = tokenExtent(nameToken, converter)
		byPos[SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))] = access
	}

	fileprivate func recordModifiers(nameToken: TokenSyntax, mask: Int32, converter: SourceLocationConverter) {
		guard mask != 0 else { return }
		let extent = tokenExtent(nameToken, converter)
		// OR (not overwrite): a declaration accumulates bits from several sources —
		// e.g. an actor keyword, `nonisolated`/`async` modifiers, and a deprecating
		// `@available` all land on the same name position.
		modifierByPos[
			SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn)), default: 0] |= mask
	}

	fileprivate func recordDeprecation(
		nameToken: TokenSyntax, message: String, converter: SourceLocationConverter
	) {
		guard !message.isEmpty else { return }  // the bit signals deprecation; the row adds the text
		let extent = tokenExtent(nameToken, converter)
		deprecationByPos[SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))] = message
	}

	fileprivate func recordAvailability(
		nameToken: TokenSyntax, attributes: AttributeListSyntax, converter: SourceLocationConverter
	) {
		guard let text = swiftAvailability(attributes) else { return }
		let extent = tokenExtent(nameToken, converter)
		availabilityByPos[SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))] = text
	}
}

// The `@available` specifications on a declaration as their raw argument text
// (e.g. `"macOS 14.0, iOS 17.0, *"`, or `"*, deprecated, message: \"use X\""`),
// multiple `@available` attributes joined by "; ". nil when the declaration has
// none. This is the Axis-3 metadata (docs/DESIGN_NODE_MODIFIERS.md) emitted into
// the node_attribute table under NodeAttributeKind.availability — purely
// syntactic, so it rides the hybrid fallback and works on broken builds.
func swiftAvailability(_ attributes: AttributeListSyntax) -> String? {
	var specs: [String] = []
	for case .attribute(let attribute) in attributes {
		guard attribute.attributeName.as(IdentifierTypeSyntax.self)?.name.text == "available"
		else { continue }
		if let arguments = attribute.arguments {
			specs.append(arguments.description.trimmingCharacters(in: .whitespacesAndNewlines))
		}
	}
	return specs.isEmpty ? nil : specs.joined(separator: "; ")
}

// A declaration's deprecation, if any: `@available(*, deprecated)` /
// `@available(platform, deprecated: version, message: "…")` — the cross-axis fact
// (DESIGN_NODE_MODIFIERS.md). The boolean drives NODE_MODIFIER_DEPRECATED; the
// `message` (empty when the attribute carries none) rides node_attribute under
// NodeAttributeKind.deprecated. `obsoleted` / `unavailable` count as deprecated.
struct SwiftDeprecation {
	let message: String
}

func swiftDeprecation(_ attributes: AttributeListSyntax) -> SwiftDeprecation? {
	for case .attribute(let attribute) in attributes {
		guard attribute.attributeName.as(IdentifierTypeSyntax.self)?.name.text == "available",
			case .availability(let spec)? = attribute.arguments
		else { continue }
		var isDeprecated = false
		var message = ""
		for entry in spec {
			switch entry.argument {
			case .token(let token):
				if ["deprecated", "obsoleted", "unavailable"].contains(token.text) {
					isDeprecated = true
				}
			case .availabilityLabeledArgument(let labeled):
				if ["deprecated", "obsoleted", "unavailable"].contains(labeled.label.text) {
					isDeprecated = true
				}
				if labeled.label.text == "message", case .string(let literal) = labeled.value {
					message = literal.segments.trimmedDescription
				}
			default:
				break
			}
		}
		if isDeprecated {
			return SwiftDeprecation(
				message: message.trimmingCharacters(in: .whitespacesAndNewlines))
		}
	}
	return nil
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

	private func record(
		_ nameToken: TokenSyntax, _ modifiers: DeclModifierListSyntax, _ attributes: AttributeListSyntax
	) {
		map.record(nameToken: nameToken, access: swiftAccessKind(modifiers), converter: converter)
		map.recordAvailability(nameToken: nameToken, attributes: attributes, converter: converter)
		if let deprecation = swiftDeprecation(attributes) {
			map.recordModifiers(nameToken: nameToken, mask: NodeModifier.deprecated, converter: converter)
			map.recordDeprecation(
				nameToken: nameToken, message: deprecation.message, converter: converter)
		}
	}

	override func visit(_ node: StructDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes); return .visitChildren
	}
	override func visit(_ node: ClassDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes); return .visitChildren
	}
	override func visit(_ node: ActorDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes)
		map.recordModifiers(nameToken: node.name, mask: NodeModifier.actor, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: EnumDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes); return .visitChildren
	}
	override func visit(_ node: ProtocolDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes); return .visitChildren
	}
	override func visit(_ node: TypeAliasDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes); return .skipChildren
	}
	override func visit(_ node: FunctionDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.name, node.modifiers, node.attributes)
		map.recordModifiers(
			nameToken: node.name,
			mask: swiftNodeModifiers(
				node.modifiers, isAsync: node.signature.effectSpecifiers?.asyncSpecifier != nil),
			converter: converter)
		return .visitChildren
	}
	override func visit(_ node: InitializerDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.initKeyword, node.modifiers, node.attributes)
		map.recordModifiers(
			nameToken: node.initKeyword,
			mask: swiftNodeModifiers(
				node.modifiers, isAsync: node.signature.effectSpecifiers?.asyncSpecifier != nil),
			converter: converter)
		return .visitChildren
	}
	override func visit(_ node: SubscriptDeclSyntax) -> SyntaxVisitorContinueKind {
		record(node.subscriptKeyword, node.modifiers, node.attributes)
		map.recordModifiers(
			nameToken: node.subscriptKeyword,
			mask: swiftNodeModifiers(node.modifiers, isAsync: false), converter: converter)
		return .visitChildren
	}
	override func visit(_ node: VariableDeclSyntax) -> SyntaxVisitorContinueKind {
		let mask = swiftNodeModifiers(node.modifiers, isAsync: false)
		for binding in node.bindings {
			if let pattern = binding.pattern.as(IdentifierPatternSyntax.self) {
				record(pattern.identifier, node.modifiers, node.attributes)
				map.recordModifiers(nameToken: pattern.identifier, mask: mask, converter: converter)
			}
		}
		return .skipChildren
	}
	override func visit(_ node: EnumCaseDeclSyntax) -> SyntaxVisitorContinueKind {
		// A case takes the enum's access; it carries no modifiers of its own, but
		// can carry its own @available (e.g. a case added in a later OS version).
		for element in node.elements {
			map.record(nameToken: element.name, access: AccessKind.default_, converter: converter)
			map.recordAvailability(
				nameToken: element.name, attributes: node.attributes, converter: converter)
			if let deprecation = swiftDeprecation(node.attributes) {
				map.recordModifiers(
					nameToken: element.name, mask: NodeModifier.deprecated, converter: converter)
				map.recordDeprecation(
					nameToken: element.name, message: deprecation.message, converter: converter)
			}
		}
		return .skipChildren
	}
}
