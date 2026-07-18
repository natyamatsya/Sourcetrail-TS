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
	private var positions: Set<SyntaxPos> = []

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
		positions.contains(pos)
	}

	fileprivate func add(_ pos: SyntaxPos) {
		positions.insert(pos)
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
		if let token = attributeNameToken(node.attributeName) {
			let extent = tokenExtent(token, converter)
			map.add(SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn)))
		}
		return .visitChildren
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
}
