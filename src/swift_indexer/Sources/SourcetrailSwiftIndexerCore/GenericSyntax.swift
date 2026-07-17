import Foundation
import SwiftParser
import SwiftSyntax

// SW11 — generic-parameter tier + constraints. The Swift analog of the Rust
// indexer's lifetime/generic handling (collector.rs `collect_generic_params`):
// every generic parameter becomes a NODE_TYPE_PARAMETER member of its owner, and
// its bounds become edges (conformance/class bound → INHERITANCE, same-type
// `where T.Element == U` → TYPE_USAGE). Generic-param lists and where clauses
// are purely syntactic, so SwiftSyntax supplies them — for every file, semantic
// or not (params ride both passes; constraints only the semantic pass, which can
// resolve the bound's target through the index).

// Whether generic use sites (`Base<Arg>`) get EDGE_TYPE_ARGUMENT edges, mirroring
// the Rust indexer's tri-state specialization scope. `local` restricts them to
// applications whose base type is defined outside the SDK/stdlib (the package or
// its SPM deps) to keep stdlib containers (Array<Int>, Optional<String>) from
// flooding the graph.
package enum SpecializationScope: String {
	case off
	case local
	case all

	// Unknown / empty wire values default to `local`, matching the Rust default.
	package static func parse(_ raw: String) -> SpecializationScope {
		SpecializationScope(rawValue: raw) ?? .local
	}
}

// One generic parameter: its name and the exact extent of its name token.
struct GenericParam {
	let name: String
	let extent: SourceExtent
}

// A constraint edge to emit in the semantic pass: from the generic parameter
// `paramName` of the owner whose name token sits at `ownerPos`, to the type whose
// base-identifier reference occurs at `targetPos` (which matches the index's
// reference occurrence for that type, so the target resolves through the store).
struct GenericConstraint {
	let ownerPos: SyntaxPos
	let paramName: String
	let targetPos: SyntaxPos
	let targetExtent: SourceExtent
	let edgeKind: Int32
}

// (SyntaxPos — the shared UTF-8 byte-column source position — lives in
// SyntaxDecls.swift.)

// Per-file generic facts, keyed to owner/constraint source positions so the
// semantic pass can join them to store occurrences it already resolves.
final class GenericParamMap {
	// owner name-token pos → its generic parameters (in declaration order).
	private(set) var paramsByOwner: [SyntaxPos: [GenericParam]] = [:]
	// bound edges to emit once the owner + target nodes are known.
	private(set) var constraints: [GenericConstraint] = []
	// positions whose reference occurrence is a constraint target — the semantic
	// pass suppresses its default container→target edge there and lets
	// emitGenerics attach the edge to the parameter instead.
	private(set) var constraintTargetPositions: Set<SyntaxPos> = []

	static func build(path: String) -> GenericParamMap {
		let map = GenericParamMap()
		guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
			return map
		}
		let tree = Parser.parse(source: source)
		let converter = SourceLocationConverter(fileName: path, tree: tree)
		GenericVisitor(map: map, converter: converter).walk(tree)
		return map
	}

	func isConstraintTarget(_ pos: SyntaxPos) -> Bool {
		constraintTargetPositions.contains(pos)
	}

	fileprivate func addParams(ownerPos: SyntaxPos, params: [GenericParam]) {
		paramsByOwner[ownerPos] = params
	}

	fileprivate func addConstraint(_ constraint: GenericConstraint) {
		constraints.append(constraint)
		constraintTargetPositions.insert(constraint.targetPos)
	}
}

// -- shared type-shape helpers ---------------------------------------------

// The identifier token that IndexStoreDB reports a type reference at: the bare
// name for `Collection`, the trailing member for `Swift.Collection`.
private func baseIdentifierToken(of type: TypeSyntax) -> TokenSyntax? {
	if let ident = type.as(IdentifierTypeSyntax.self) {
		return ident.name
	}
	if let member = type.as(MemberTypeSyntax.self) {
		return member.name
	}
	return nil
}

// The leftmost identifier — the root that a where-clause left side is rooted in
// (`T` for both `T` and `T.Element`), used to attribute a constraint to a param.
private func rootIdentifier(of type: TypeSyntax) -> String? {
	if let ident = type.as(IdentifierTypeSyntax.self) {
		return ident.name.text
	}
	if let member = type.as(MemberTypeSyntax.self) {
		return rootIdentifier(of: member.baseType)
	}
	return nil
}

// A bound may be a protocol composition (`A & B`) → one target per element.
private func constraintTargetTokens(of type: TypeSyntax) -> [TokenSyntax] {
	if let composition = type.as(CompositionTypeSyntax.self) {
		return composition.elements.compactMap { baseIdentifierToken(of: $0.type) }
	}
	if let token = baseIdentifierToken(of: type) {
		return [token]
	}
	return []
}

// Walks declarations that own a generic parameter clause and records their
// parameters and same-owner constraints. Extension / protocol where clauses
// (conditional & retroactive conformance) are SW12; skipped here.
private final class GenericVisitor: SyntaxVisitor {
	private let map: GenericParamMap
	private let converter: SourceLocationConverter

	init(map: GenericParamMap, converter: SourceLocationConverter) {
		self.map = map
		self.converter = converter
		super.init(viewMode: .sourceAccurate)
	}

	private func pos(of token: TokenSyntax) -> SyntaxPos {
		let extent = tokenExtent(token, converter)
		return SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))
	}

	// Record params + constraints for one owner. `nameToken` must be the same
	// token the semantic definition occurrence lands on (matches DeclScopeMap).
	private func record(
		nameToken: TokenSyntax,
		clause: GenericParameterClauseSyntax?,
		whereClause: GenericWhereClauseSyntax?
	) {
		guard let clause else { return }
		let ownerPos = pos(of: nameToken)

		var params: [GenericParam] = []
		var paramNames: Set<String> = []
		for parameter in clause.parameters {
			let name = parameter.name.text
			params.append(GenericParam(name: name, extent: tokenExtent(parameter.name, converter)))
			paramNames.insert(name)
			// Inline bound: `T: Collection` / `T: A & B`.
			if let inherited = parameter.inheritedType {
				for target in constraintTargetTokens(of: inherited) {
					addConstraint(
						ownerPos: ownerPos, paramName: name, target: target,
						edgeKind: EdgeKind.inheritance)
				}
			}
		}
		map.addParams(ownerPos: ownerPos, params: params)

		// Where clause: only constraints rooted in one of this owner's params.
		guard let whereClause else { return }
		for requirement in whereClause.requirements {
			switch requirement.requirement {
			case .conformanceRequirement(let conformance):
				guard let root = rootIdentifier(of: conformance.leftType), paramNames.contains(root)
				else { continue }
				for target in constraintTargetTokens(of: conformance.rightType) {
					addConstraint(
						ownerPos: ownerPos, paramName: root, target: target,
						edgeKind: EdgeKind.inheritance)
				}
			case .sameTypeRequirement(let sameType):
				// `T == U` / `T.Element == U`: edge from whichever side roots in a
				// param to the other side's referenced type. In swift-syntax 602 each
				// side is a type-or-expression union (value generics), so cast to a
				// type and skip non-type sides.
				let leftType = Syntax(sameType.leftType).as(TypeSyntax.self)
				let rightType = Syntax(sameType.rightType).as(TypeSyntax.self)
				if let leftType, let leftRoot = rootIdentifier(of: leftType),
					paramNames.contains(leftRoot),
					let rightType, let target = baseIdentifierToken(of: rightType)
				{
					addConstraint(
						ownerPos: ownerPos, paramName: leftRoot, target: target,
						edgeKind: EdgeKind.typeUsage)
				}
				if let rightType, let rightRoot = rootIdentifier(of: rightType),
					paramNames.contains(rightRoot),
					let leftType, let target = baseIdentifierToken(of: leftType)
				{
					addConstraint(
						ownerPos: ownerPos, paramName: rightRoot, target: target,
						edgeKind: EdgeKind.typeUsage)
				}
			default:
				continue  // layout requirements have no graph target
			}
		}
	}

	private func addConstraint(
		ownerPos: SyntaxPos, paramName: String, target: TokenSyntax, edgeKind: Int32
	) {
		let extent = tokenExtent(target, converter)
		map.addConstraint(
			GenericConstraint(
				ownerPos: ownerPos,
				paramName: paramName,
				targetPos: SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn)),
				targetExtent: extent,
				edgeKind: edgeKind))
	}

	// -- owners with a generic parameter clause -----------------------------

	override func visit(_ node: StructDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.name, clause: node.genericParameterClause, whereClause: node.genericWhereClause)
		return .visitChildren
	}
	override func visit(_ node: ClassDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.name, clause: node.genericParameterClause, whereClause: node.genericWhereClause)
		return .visitChildren
	}
	override func visit(_ node: ActorDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.name, clause: node.genericParameterClause, whereClause: node.genericWhereClause)
		return .visitChildren
	}
	override func visit(_ node: EnumDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.name, clause: node.genericParameterClause, whereClause: node.genericWhereClause)
		return .visitChildren
	}
	override func visit(_ node: FunctionDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.name, clause: node.genericParameterClause, whereClause: node.genericWhereClause)
		return .visitChildren
	}
	override func visit(_ node: SubscriptDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.subscriptKeyword, clause: node.genericParameterClause, whereClause: node.genericWhereClause)
		return .visitChildren
	}
	override func visit(_ node: TypeAliasDeclSyntax) -> SyntaxVisitorContinueKind {
		record(nameToken: node.name, clause: node.genericParameterClause, whereClause: nil)
		return .skipChildren
	}
}

// SW11 (type arguments) — direct type arguments of generic use sites in type
// position (`Box<Int>`, `Dictionary<Key, Value>`). Maps each argument's
// base-identifier position to its generic base type's position, so the semantic
// pass can (a) recognize the argument as a type argument and (b) look up the
// base type's locality for the `local` scope gate. Sugar forms (`[Int]`, `T?`)
// have no written base token and are left as plain type usages.
final class GenericArgMap {
	// argument base-identifier pos → generic base-type base-identifier pos.
	private var argToBase: [SyntaxPos: SyntaxPos] = [:]

	static func build(path: String) -> GenericArgMap {
		let map = GenericArgMap()
		guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
			return map
		}
		let tree = Parser.parse(source: source)
		let converter = SourceLocationConverter(fileName: path, tree: tree)
		GenericArgVisitor(map: map, converter: converter).walk(tree)
		return map
	}

	// The generic base type of the application this position is a type argument
	// of, or nil if the position is not a type argument.
	func base(of pos: SyntaxPos) -> SyntaxPos? {
		argToBase[pos]
	}

	fileprivate func add(argPos: SyntaxPos, basePos: SyntaxPos) {
		argToBase[argPos] = basePos
	}
}

private final class GenericArgVisitor: SyntaxVisitor {
	private let map: GenericArgMap
	private let converter: SourceLocationConverter

	init(map: GenericArgMap, converter: SourceLocationConverter) {
		self.map = map
		self.converter = converter
		super.init(viewMode: .sourceAccurate)
	}

	private func pos(of token: TokenSyntax) -> SyntaxPos {
		let extent = tokenExtent(token, converter)
		return SyntaxPos(line: Int(extent.startLine), column: Int(extent.startColumn))
	}

	private func record(baseName: TokenSyntax, arguments: GenericArgumentListSyntax) {
		let basePos = pos(of: baseName)
		for argument in arguments {
			guard let type = Syntax(argument.argument).as(TypeSyntax.self),
				let token = baseIdentifierToken(of: type)
			else { continue }
			map.add(argPos: pos(of: token), basePos: basePos)
		}
	}

	override func visit(_ node: IdentifierTypeSyntax) -> SyntaxVisitorContinueKind {
		if let clause = node.genericArgumentClause {
			record(baseName: node.name, arguments: clause.arguments)
		}
		return .visitChildren
	}

	override func visit(_ node: MemberTypeSyntax) -> SyntaxVisitorContinueKind {
		if let clause = node.genericArgumentClause {
			record(baseName: node.name, arguments: clause.arguments)
		}
		return .visitChildren
	}
}
