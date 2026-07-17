import Foundation
import SwiftParser
import SwiftSyntax

// SW10 — code-view parity. Facts a semantic occurrence index (IndexStoreDB)
// cannot provide but the Sourcetrail code view needs: the exact end of a name
// token (precise highlight extents) and the full brace-to-brace extent of a
// declaration (its SCOPE, so a whole class/function reads as one navigable
// region). Both are syntactic, so SwiftSyntax — already integrated as the SW3
// fallback — supplies them for every file, semantic or not.

// Inclusive 1-based source range in the UTF-8 byte column system IndexStoreDB
// and SwiftSyntax's SourceLocationConverter both use.
struct SourceExtent {
	let startLine: UInt32
	let startColumn: UInt32
	let endLine: UInt32
	let endColumn: UInt32
}

private func u32(_ value: Int) -> UInt32 { UInt32(max(value, 1)) }

// Exact extent of a token's text (leading/trailing trivia excluded). endColumn
// is inclusive: `endLocation.column` points one past the last byte.
func tokenExtent(_ token: TokenSyntax, _ conv: SourceLocationConverter) -> SourceExtent {
	let start = token.startLocation(converter: conv)
	let end = token.endLocation(converter: conv)
	return SourceExtent(
		startLine: u32(start.line),
		startColumn: u32(start.column),
		endLine: u32(end.line),
		endColumn: UInt32(max(end.column - 1, start.column))
	)
}

// Full declaration extent (keyword through closing brace) — the SCOPE.
func nodeExtent(_ node: some SyntaxProtocol, _ conv: SourceLocationConverter) -> SourceExtent {
	let start = node.startLocation(converter: conv)
	let end = node.endLocation(converter: conv)
	return SourceExtent(
		startLine: u32(start.line),
		startColumn: u32(start.column),
		endLine: u32(end.line),
		endColumn: UInt32(max(end.column - 1, 1))
	)
}

private struct SyntaxPos: Hashable {
	let line: Int
	let column: Int
}

// Name-token position → (precise name extent, full scope extent) for every
// declaration in a file. The semantic pass keys IndexStoreDB definition
// occurrences by the same (line, utf8-column), so a hit lets it emit a precise
// TOKEN and a SCOPE location instead of a single approximate token.
final class DeclScopeMap {
	private var byNamePos: [SyntaxPos: (name: SourceExtent, scope: SourceExtent)] = [:]

	static func build(path: String) -> DeclScopeMap {
		let map = DeclScopeMap()
		guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
			return map
		}
		let tree = Parser.parse(source: source)
		let converter = SourceLocationConverter(fileName: path, tree: tree)
		DeclScopeVisitor(map: map, converter: converter).walk(tree)
		return map
	}

	func extents(line: Int, column: Int) -> (name: SourceExtent, scope: SourceExtent)? {
		byNamePos[SyntaxPos(line: line, column: column)]
	}

	fileprivate func record(
		nameToken: TokenSyntax, scope: some SyntaxProtocol, converter: SourceLocationConverter
	) {
		let name = tokenExtent(nameToken, converter)
		byNamePos[SyntaxPos(line: Int(name.startLine), column: Int(name.startColumn))] =
			(name, nodeExtent(scope, converter))
	}
}

// Records the name token + full extent of each declaration. The name-token
// selection MUST match what IndexStoreDB reports as a definition's location
// (the identifier, or the init/subscript/deinit keyword) or the semantic
// lookup misses.
private final class DeclScopeVisitor: SyntaxVisitor {
	private let map: DeclScopeMap
	private let converter: SourceLocationConverter

	init(map: DeclScopeMap, converter: SourceLocationConverter) {
		self.map = map
		self.converter = converter
		super.init(viewMode: .sourceAccurate)
	}

	override func visit(_ node: StructDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: ClassDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: ActorDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: EnumDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: ProtocolDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: TypeAliasDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .skipChildren
	}
	override func visit(_ node: FunctionDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.name, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: InitializerDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.initKeyword, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: SubscriptDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.subscriptKeyword, scope: node, converter: converter)
		return .visitChildren
	}
	override func visit(_ node: DeinitializerDeclSyntax) -> SyntaxVisitorContinueKind {
		map.record(nameToken: node.deinitKeyword, scope: node, converter: converter)
		return .skipChildren
	}
	override func visit(_ node: VariableDeclSyntax) -> SyntaxVisitorContinueKind {
		for binding in node.bindings {
			if let pattern = binding.pattern.as(IdentifierPatternSyntax.self) {
				map.record(nameToken: pattern.identifier, scope: node, converter: converter)
			}
		}
		return .skipChildren
	}
	override func visit(_ node: EnumCaseDeclSyntax) -> SyntaxVisitorContinueKind {
		for element in node.elements {
			map.record(nameToken: element.name, scope: element, converter: converter)
		}
		return .skipChildren
	}
}
