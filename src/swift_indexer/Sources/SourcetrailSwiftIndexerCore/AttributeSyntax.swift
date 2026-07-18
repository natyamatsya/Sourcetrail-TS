import Foundation
import SwiftParser
import SwiftSyntax

// SW14 — attribute-driven relations. A custom attribute application (a property
// wrapper `@Clamped`, a result builder `@ViewBuilder`, a global actor
// `@MainActor`) references a type at the attribute's name position, which the
// index already resolves — the annotated declaration is the containing symbol.
// The only thing the store cannot tell us is that the reference *is* an
// attribute application (rather than an ordinary type usage); SwiftSyntax marks
// those positions so the semantic pass can emit EDGE_ANNOTATION_USAGE there.
//
// This is the reusable attribute-position map SW13 (global-actor isolation) and
// SW16 (`@available`) also consume. Built-in attributes (`@available`, `@objc`)
// name no type and simply never resolve to a node, so marking every attribute
// position is safe — only ones resolving to a type node produce an edge.
final class AttributeMap {
	// attribute-name position → name-token position of the declaration it is
	// attached to. The declaration is the annotation-usage edge's source — the
	// store's containing-symbol relation is unreliable for type-level attributes
	// (`@MainActor class …`), so SwiftSyntax supplies it.
	private var annotatedDecl: [SyntaxPos: SyntaxPos] = [:]

	static func build(path: String) -> AttributeMap {
		let map = AttributeMap()
		guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
			return map
		}
		let tree = Parser.parse(source: source)
		let converter = SourceLocationConverter(fileName: path, tree: tree)
		AttributeVisitor(map: map, converter: converter).walk(tree)
		return map
	}

	func isAttribute(_ pos: SyntaxPos) -> Bool {
		annotatedDecl[pos] != nil
	}

	// The declaration an attribute at `pos` is attached to.
	func annotatedDecl(of pos: SyntaxPos) -> SyntaxPos? {
		annotatedDecl[pos]
	}

	fileprivate func add(attributePos: SyntaxPos, declPos: SyntaxPos) {
		annotatedDecl[attributePos] = declPos
	}
}

private final class AttributeVisitor: SyntaxVisitor {
	private let map: AttributeMap
	private let converter: SourceLocationConverter

	init(map: AttributeMap, converter: SourceLocationConverter) {
		self.map = map
		self.converter = converter
		super.init(viewMode: .sourceAccurate)
	}

	override func visit(_ node: AttributeSyntax) -> SyntaxVisitorContinueKind {
		guard let nameToken = attributeNameToken(node.attributeName),
			let declToken = annotatedDeclNameToken(node)
		else {
			return .visitChildren
		}
		map.add(attributePos: pos(of: nameToken), declPos: pos(of: declToken))
		return .visitChildren
	}

	private func pos(of token: TokenSyntax) -> SyntaxPos {
		let extent = tokenExtent(token, converter)
		return SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))
	}

	// The identifier the index reports the attribute's type reference at: the bare
	// name for `@Clamped`, the trailing member for `@Foo.Bar`.
	private func attributeNameToken(_ type: TypeSyntax) -> TokenSyntax? {
		if let ident = type.as(IdentifierTypeSyntax.self) {
			return ident.name
		}
		if let member = type.as(MemberTypeSyntax.self) {
			return member.name
		}
		return nil
	}

	// The name token of the declaration this attribute is attached to — the first
	// declaration ancestor (attributes bind to the immediately enclosing decl).
	// Must match the token the index reports that declaration's definition at
	// (same selection as DeclScopeMap).
	private func annotatedDeclNameToken(_ attribute: AttributeSyntax) -> TokenSyntax? {
		var current: Syntax? = attribute.parent
		while let node = current {
			if let d = node.as(StructDeclSyntax.self) { return d.name }
			if let d = node.as(ClassDeclSyntax.self) { return d.name }
			if let d = node.as(ActorDeclSyntax.self) { return d.name }
			if let d = node.as(EnumDeclSyntax.self) { return d.name }
			if let d = node.as(ProtocolDeclSyntax.self) { return d.name }
			if let d = node.as(TypeAliasDeclSyntax.self) { return d.name }
			if let d = node.as(FunctionDeclSyntax.self) { return d.name }
			if let d = node.as(InitializerDeclSyntax.self) { return d.initKeyword }
			if let d = node.as(SubscriptDeclSyntax.self) { return d.subscriptKeyword }
			if let d = node.as(VariableDeclSyntax.self) {
				return d.bindings.first?.pattern.as(IdentifierPatternSyntax.self)?.identifier
			}
			if let d = node.as(EnumCaseDeclSyntax.self) { return d.elements.first?.name }
			current = node.parent
		}
		return nil
	}
}
