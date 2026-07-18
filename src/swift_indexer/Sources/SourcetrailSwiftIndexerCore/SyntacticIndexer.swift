import Foundation
import SwiftParser
import SwiftSyntax

// Syntactic fallback (SW3): declaration structure for files without an
// up-to-date index unit — broken or not-yet-built code still gets nodes,
// member edges, and definition occurrences. No cross-file references. Name
// spelling MUST match the semantic engine's (index-store display names,
// e.g. `greet()`, `init(x:)`) or the same symbol forks into two nodes.

enum SyntacticIndexer {
	static func indexFile(
		path: String,
		moduleName: String,
		builder: StorageBuilder
	) {
		guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
			builder.recordError(
				message: "syntactic fallback could not read file",
				translationUnit: path
			)
			builder.appendPlainFile(path: path, complete: false)
			return
		}

		// complete=false: the app re-indexes once the build heals and the
		// semantic pass takes over.
		let fileNodeId = builder.fileNodeId(path: path, complete: false)
		builder.recordError(
			message: "no up-to-date index unit; declarations indexed syntactically",
			translationUnit: path
		)

		let tree = Parser.parse(source: source)
		let converter = SourceLocationConverter(fileName: path, tree: tree)
		let visitor = DeclVisitor(
			moduleName: moduleName,
			builder: builder,
			fileNodeId: fileNodeId,
			converter: converter
		)
		visitor.walk(tree)
	}

	private final class DeclVisitor: SyntaxVisitor {
		private let builder: StorageBuilder
		private let fileNodeId: Int64
		private let converter: SourceLocationConverter
		/// Name-part stack; starts with the module.
		private var scope: [String]

		init(
			moduleName: String,
			builder: StorageBuilder,
			fileNodeId: Int64,
			converter: SourceLocationConverter
		) {
			self.builder = builder
			self.fileNodeId = fileNodeId
			self.converter = converter
			self.scope = [moduleName]
			super.init(viewMode: .sourceAccurate)
			_ = builder.nodeId(parts: [moduleName], kind: NodeKind.module)
		}

		// -- shared emission ------------------------------------------------

		private func emit(
			name: String, kind: Int32, nameToken: TokenSyntax, decl: some SyntaxProtocol,
			access: Int32 = AccessKind.default_
		) -> Int64 {
			let parts = scope + [name]
			let nodeId = builder.nodeId(parts: parts, kind: kind)
			builder.recordSymbol(nodeId: nodeId, definitionKind: DefinitionKind.explicit)
			// SW16: declared access level (purely syntactic — works on broken builds).
			builder.recordComponentAccess(nodeId: nodeId, access: access)

			let parentKind = scope.count == 1 ? NodeKind.module : NodeKind.symbol
			let parentId = builder.nodeId(parts: scope, kind: parentKind)
			_ = builder.edgeId(type: EdgeKind.member, source: parentId, target: nodeId)

			// SW10: precise name TOKEN (exact byte extent, not char count) plus a
			// SCOPE spanning the whole declaration — the same pair the semantic
			// pass emits, so the code view treats a syntactic-only file the same.
			record(nodeId, tokenExtent(nameToken, converter), LocationKind.token)
			record(nodeId, nodeExtent(decl, converter), LocationKind.scope)
			return nodeId
		}

		private func record(_ elementId: Int64, _ extent: SourceExtent, _ type: Int32) {
			builder.recordOccurrence(
				elementId: elementId,
				fileNodeId: fileNodeId,
				startLine: extent.startLine,
				startCol: extent.startColumn,
				endLine: extent.endLine,
				endCol: extent.endColumn,
				locationType: type
			)
		}

		// SW11: a generic owner's type parameters as NODE_TYPE_PARAMETER members
		// with precise name tokens. Bounds are not resolved here — the syntactic
		// fallback carries no cross-reference edges (see the semantic pass).
		private func emitGenericParams(
			ownerParts: [String], clause: GenericParameterClauseSyntax?
		) {
			guard let clause else { return }
			let ownerId = builder.nodeId(parts: ownerParts, kind: NodeKind.symbol)
			for parameter in clause.parameters {
				let paramId = builder.nodeId(
					parts: ownerParts + [parameter.name.text], kind: NodeKind.typeParameter)
				builder.recordSymbol(nodeId: paramId, definitionKind: DefinitionKind.explicit)
				_ = builder.edgeId(type: EdgeKind.member, source: ownerId, target: paramId)
				record(paramId, tokenExtent(parameter.name, converter), LocationKind.token)
			}
		}

		private func push(_ name: String) {
			scope.append(name)
		}

		// Index-store display names carry parameter labels: `f(x:_:)`.
		private func functionName(_ baseName: String, _ parameters: FunctionParameterListSyntax)
			-> String
		{
			let labels = parameters.map { parameter in
				(parameter.firstName.tokenKind == .wildcard ? "_" : parameter.firstName.text) + ":"
			}
			return baseName + "(" + labels.joined() + ")"
		}

		// -- types ----------------------------------------------------------

		override func visit(_ node: StructDeclSyntax) -> SyntaxVisitorContinueKind {
			_ = emit(name: node.name.text, kind: NodeKind.struct, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			emitGenericParams(ownerParts: scope + [node.name.text], clause: node.genericParameterClause)
			push(node.name.text)
			return .visitChildren
		}
		override func visitPost(_ node: StructDeclSyntax) { scope.removeLast() }

		override func visit(_ node: ClassDeclSyntax) -> SyntaxVisitorContinueKind {
			_ = emit(name: node.name.text, kind: NodeKind.class, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			emitGenericParams(ownerParts: scope + [node.name.text], clause: node.genericParameterClause)
			push(node.name.text)
			return .visitChildren
		}
		override func visitPost(_ node: ClassDeclSyntax) { scope.removeLast() }

		override func visit(_ node: ActorDeclSyntax) -> SyntaxVisitorContinueKind {
			let nodeId = emit(name: node.name.text, kind: NodeKind.class, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			builder.addNodeModifier(nodeId: nodeId, modifier: NodeModifier.actor)
			emitGenericParams(ownerParts: scope + [node.name.text], clause: node.genericParameterClause)
			push(node.name.text)
			return .visitChildren
		}
		override func visitPost(_ node: ActorDeclSyntax) { scope.removeLast() }

		override func visit(_ node: EnumDeclSyntax) -> SyntaxVisitorContinueKind {
			_ = emit(name: node.name.text, kind: NodeKind.enum, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			emitGenericParams(ownerParts: scope + [node.name.text], clause: node.genericParameterClause)
			push(node.name.text)
			return .visitChildren
		}
		override func visitPost(_ node: EnumDeclSyntax) { scope.removeLast() }

		override func visit(_ node: ProtocolDeclSyntax) -> SyntaxVisitorContinueKind {
			_ = emit(name: node.name.text, kind: NodeKind.interface, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			push(node.name.text)
			return .visitChildren
		}
		override func visitPost(_ node: ProtocolDeclSyntax) { scope.removeLast() }

		// Extension members attach to the extended type (module-qualified
		// spelling of the extended type name), matching the semantic side's
		// extendedBy redirection for same-module extensions.
		override func visit(_ node: ExtensionDeclSyntax) -> SyntaxVisitorContinueKind {
			let typeName = node.extendedType.trimmedDescription
			let parts = typeName.components(separatedBy: ".")
			scope = [scope[0]] + parts
			return .visitChildren
		}
		override func visitPost(_ node: ExtensionDeclSyntax) {
			scope = [scope[0]]
		}

		override func visit(_ node: TypeAliasDeclSyntax) -> SyntaxVisitorContinueKind {
			_ = emit(name: node.name.text, kind: NodeKind.typedef, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			emitGenericParams(ownerParts: scope + [node.name.text], clause: node.genericParameterClause)
			return .skipChildren
		}

		// -- members --------------------------------------------------------

		override func visit(_ node: FunctionDeclSyntax) -> SyntaxVisitorContinueKind {
			let name = functionName(node.name.text, node.signature.parameterClause.parameters)
			let kind = scope.count == 1 ? NodeKind.function : NodeKind.method
			_ = emit(name: name, kind: kind, nameToken: node.name, decl: node,
				access: swiftAccessKind(node.modifiers))
			emitGenericParams(ownerParts: scope + [name], clause: node.genericParameterClause)
			push(name)
			return .visitChildren
		}
		override func visitPost(_ node: FunctionDeclSyntax) { scope.removeLast() }

		override func visit(_ node: InitializerDeclSyntax) -> SyntaxVisitorContinueKind {
			let name = functionName("init", node.signature.parameterClause.parameters)
			_ = emit(name: name, kind: NodeKind.method, nameToken: node.initKeyword, decl: node,
				access: swiftAccessKind(node.modifiers))
			push(name)
			return .visitChildren
		}
		override func visitPost(_ node: InitializerDeclSyntax) { scope.removeLast() }

		override func visit(_ node: SubscriptDeclSyntax) -> SyntaxVisitorContinueKind {
			let name = functionName("subscript", node.parameterClause.parameters)
			_ = emit(name: name, kind: NodeKind.method, nameToken: node.subscriptKeyword, decl: node,
				access: swiftAccessKind(node.modifiers))
			emitGenericParams(ownerParts: scope + [name], clause: node.genericParameterClause)
			push(name)
			return .visitChildren
		}
		override func visitPost(_ node: SubscriptDeclSyntax) { scope.removeLast() }

		override func visit(_ node: VariableDeclSyntax) -> SyntaxVisitorContinueKind {
			for binding in node.bindings {
				guard let pattern = binding.pattern.as(IdentifierPatternSyntax.self) else {
					continue
				}
				let kind = scope.count == 1 ? NodeKind.globalVariable : NodeKind.field
				_ = emit(name: pattern.identifier.text, kind: kind, nameToken: pattern.identifier, decl: node,
					access: swiftAccessKind(node.modifiers))
			}
			// Accessors/initializer expressions carry no declarations we index.
			return .skipChildren
		}

		override func visit(_ node: EnumCaseDeclSyntax) -> SyntaxVisitorContinueKind {
			for element in node.elements {
				_ = emit(
					name: element.name.text,
					kind: NodeKind.enumConstant,
					nameToken: element.name,
					decl: element
				)
			}
			return .skipChildren
		}

		// Function bodies contain no further type declarations we can name
		// stably; local funcs/types are intentionally skipped (they are
		// file-private detail, and the semantic pass names them properly).
		override func visit(_ node: CodeBlockSyntax) -> SyntaxVisitorContinueKind {
			.skipChildren
		}
	}
}
