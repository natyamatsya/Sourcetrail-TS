// CMakeFileAPIReader.cpp
//
// Reads the CMake File-based API (cmake.org/cmake/help/latest/manual/cmake-file-api.7.html)
// reply from a configured build directory.
//
// Query/reply directory layout:
//   <build>/.cmake/api/v1/query/client-sourcetrail/query.json   (written by us)
//   <build>/.cmake/api/v1/reply/index-<hash>.json               (written by cmake)
//   <build>/.cmake/api/v1/reply/codemodel-v2-<hash>.json
//   <build>/.cmake/api/v1/reply/target-v2-<name>-<hash>.json
//   <build>/.cmake/api/v1/reply/cmakeFiles-v1-<hash>.json

#include "CMakeFileAPIReader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include "json-query/JSONQuery"
#include "logging.h"

using namespace json_query;

namespace
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QJsonDocument readJsonFile(const FilePath& path)
{
	QFile file{QString::fromStdString(path.str())};
	if (!file.open(QIODevice::ReadOnly))
	{
		LOG_WARNING("CMakeFileAPIReader: cannot open " + path.str());
		return {};
	}
	QJsonParseError err{};
	const auto doc{QJsonDocument::fromJson(file.readAll(), &err)};
	if (err.error != QJsonParseError::NoError)
	{
		LOG_WARNING(
			"CMakeFileAPIReader: JSON parse error in " + path.str() + ": " +
			err.errorString().toStdString());
		return {};
	}
	return doc;
}

// Returns the lexicographically last file in dir matching the given prefix,
// or empty FilePath.
FilePath findFileWithPrefix(const FilePath& dir, const std::string& prefix)
{
	const QDir qdir{QString::fromStdString(dir.str())};
	const auto entries{qdir.entryList(
		{QString::fromStdString(prefix + "*.json")}, QDir::Files, QDir::Name)};
	if (entries.isEmpty())
		return {};
	return dir.getConcatenated("/" + entries.last().toStdString());
}

// Glob match: empty pattern matches everything, otherwise simple wildcard.
bool matchesGlob(const std::string& name, const std::string& glob)
{
	if (glob.empty())
		return true;
	const auto pattern{
		QRegularExpression::wildcardToRegularExpression(QString::fromStdString(glob))};
	return QRegularExpression{pattern}.match(QString::fromStdString(name)).hasMatch();
}

// ---------------------------------------------------------------------------
// JSON Schema for codemodel-v2 (minimal — validates kind and version)
// ---------------------------------------------------------------------------

const QJsonObject k_codemodelSchema{
	{"type", "object"},
	{"required", QJsonArray{"kind", "version", "configurations"}},
	{"properties",
	 QJsonObject{
		 {"kind", QJsonObject{{"type", "string"}, {"const", "codemodel"}}},
		 {"version",
		  QJsonObject{
			  {"type", "object"},
			  {"required", QJsonArray{"major"}},
			  {"properties",
			   QJsonObject{{"major", QJsonObject{{"type", "integer"}, {"minimum", 2}}}}}}},
		 {"configurations", QJsonObject{{"type", "array"}}},
	 }}};

}	 // namespace

// ---------------------------------------------------------------------------
// CMakeFileAPIReader
// ---------------------------------------------------------------------------

CMakeFileAPIReader::CMakeFileAPIReader(const FilePath& buildDir)
	: m_buildDir{buildDir}
	, m_queryDir{buildDir.getConcatenated("/.cmake/api/v1/query/client-sourcetrail")}
	, m_replyDir{buildDir.getConcatenated("/.cmake/api/v1/reply")}
{
}

const FilePath& CMakeFileAPIReader::buildDir() const
{
	return m_buildDir;
}

bool CMakeFileAPIReader::hasReply() const
{
	return !findIndexFile().empty();
}

std::vector<std::string> CMakeFileAPIReader::discoverPresets(const FilePath& sourceDir)
{
	std::vector<std::string> result{};

	// Read both CMakePresets.json and CMakeUserPresets.json; merge results.
	const std::array<std::string, 2> filenames{
		"CMakePresets.json", "CMakeUserPresets.json"};

	for (const auto& filename : filenames)
	{
		const auto presetsPath{sourceDir.getConcatenated("/" + filename)};
		if (!presetsPath.exists())
			continue;

		const auto doc{readJsonFile(presetsPath)};
		if (doc.isNull())
			continue;

		for (const auto& val : doc.object()["configurePresets"].toArray())
		{
			const auto preset{val.toObject()};
			if (preset["hidden"].toBool(false))
				continue;
			const auto name{preset["name"].toString().toStdString()};
			if (!name.empty())
				result.push_back(name);
		}
	}

	return result;
}

// Expand the subset of CMake preset macro variables we care about.
// Spec: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html#macro-expansion
static QString expandPresetMacros(
	const QString& value, const QString& sourceDir, const QString& presetName)
{
	QString result{value};
	result.replace(QStringLiteral("${sourceDir}"), sourceDir);
	result.replace(QStringLiteral("${presetName}"), presetName);
	// ${sourceDirName} = last path component of sourceDir
	result.replace(
		QStringLiteral("${sourceDirName}"), QDir{sourceDir}.dirName());
	// ${hostSystemName} — best-effort
#if defined(Q_OS_WIN)
	result.replace(QStringLiteral("${hostSystemName}"), QStringLiteral("Windows"));
#elif defined(Q_OS_MAC)
	result.replace(QStringLiteral("${hostSystemName}"), QStringLiteral("Darwin"));
#else
	result.replace(QStringLiteral("${hostSystemName}"), QStringLiteral("Linux"));
#endif
	return result;
}

// Build a flat map of all configure presets from CMakePresets.json and
// CMakeUserPresets.json found in sourceDir.
static QMap<QString, QJsonObject> collectPresets(const FilePath& sourceDir)
{
	QMap<QString, QJsonObject> result{};
	for (const auto& filename :
		 {std::string{"CMakePresets.json"}, std::string{"CMakeUserPresets.json"}})
	{
		const auto path{sourceDir.getConcatenated("/" + filename)};
		if (!path.exists())
			continue;
		const auto doc{readJsonFile(path)};
		if (doc.isNull())
			continue;
		for (const auto& val : doc.object()["configurePresets"].toArray())
		{
			const auto obj{val.toObject()};
			const auto name{obj["name"].toString()};
			if (!name.isEmpty())
				result.insert(name, obj);
		}
	}
	return result;
}

// Walk the inherits chain to find the first preset that defines binaryDir.
static QString findBinaryDirTemplate(
	const QString& presetName,
	const QMap<QString, QJsonObject>& presets,
	QSet<QString>& visited)
{
	if (visited.contains(presetName))
		return {};
	visited.insert(presetName);

	const auto it{presets.find(presetName)};
	if (it == presets.end())
		return {};

	const auto& obj{*it};
	if (obj.contains(QStringLiteral("binaryDir")))
		return obj["binaryDir"].toString();

	// Follow inherits (string or array of strings).
	const auto inheritsVal{obj["inherits"]};
	QStringList parents{};
	if (inheritsVal.isString())
		parents << inheritsVal.toString();
	else if (inheritsVal.isArray())
		for (const auto& v : inheritsVal.toArray())
			parents << v.toString();

	for (const auto& parent : parents)
	{
		const auto found{findBinaryDirTemplate(parent, presets, visited)};
		if (!found.isEmpty())
			return found;
	}
	return {};
}

FilePath CMakeFileAPIReader::resolveBinaryDir(
	const FilePath& sourceDir, const std::string& presetName)
{
	const auto presets{collectPresets(sourceDir)};
	const auto qPresetName{QString::fromStdString(presetName)};
	const auto qSourceDir{QString::fromStdString(sourceDir.str())};

	QSet<QString> visited{};
	const auto tmpl{findBinaryDirTemplate(qPresetName, presets, visited)};
	if (tmpl.isEmpty())
	{
		LOG_WARNING(
			"CMakeFileAPIReader: no binaryDir found for preset '" + presetName + "'");
		return {};
	}

	const auto expanded{expandPresetMacros(tmpl, qSourceDir, qPresetName)};
	if (expanded.isEmpty())
		return {};

	// binaryDir may be relative to sourceDir.
	const FilePath result{expanded.toStdString()};
	if (result.isAbsolute())
		return result;
	return sourceDir.getConcatenated("/" + expanded.toStdString());
}

bool CMakeFileAPIReader::ensureReply(
	std::function<void(const std::string&)> progress,
	const FilePath& sourceDir,
	const std::string& presetName)
{
	// 1. Write the query file so CMake knows what to generate.
	{
		const QString queryPath{
			QString::fromStdString(m_queryDir.str()) + "/query.json"};
		QDir{}.mkpath(QString::fromStdString(m_queryDir.str()));
		QFile f{queryPath};
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			LOG_ERROR("CMakeFileAPIReader: cannot write query file: " + queryPath.toStdString());
			return false;
		}
		f.write(R"({"requests":[{"kind":"codemodel","version":2},{"kind":"cmakeFiles","version":1},{"kind":"toolchains","version":1}]})"
				"\n");
	}

	if (hasReply())
		return true;

	// 2. No reply yet — run cmake to generate it.
	if (progress)
		progress("Running cmake to generate File API reply...");

	QProcess proc{};
	QStringList args{};
	if (!sourceDir.empty() && !presetName.empty())
	{
		// Preset-aware invocation.
		proc.setWorkingDirectory(QString::fromStdString(sourceDir.str()));
		args << QStringLiteral("-S") << QString::fromStdString(sourceDir.str())
			 << QStringLiteral("--preset") << QString::fromStdString(presetName);
	}
	else
	{
		// Legacy fallback: cmake <buildDir>
		proc.setWorkingDirectory(QString::fromStdString(m_buildDir.str()));
		args << QString::fromStdString(m_buildDir.str());
	}

	proc.start("cmake", args);
	if (!proc.waitForFinished(60'000))
	{
		LOG_ERROR("CMakeFileAPIReader: cmake timed out or failed to start");
		return false;
	}
	if (proc.exitCode() != 0)
	{
		LOG_WARNING(
			"CMakeFileAPIReader: cmake exited with code " +
			std::to_string(proc.exitCode()) + ": " +
			proc.readAllStandardError().toStdString());
		return false;
	}

	if (progress)
		progress("CMake File API reply generated.");

	return hasReply();
}

FilePath CMakeFileAPIReader::findIndexFile() const
{
	return findFileWithPrefix(m_replyDir, "index-");
}

FilePath CMakeFileAPIReader::findToolchainsFile() const
{
	return findFileWithPrefix(m_replyDir, "toolchains-v1-");
}

std::vector<CMakeFileAPIReader::SourceEntry> CMakeFileAPIReader::getSources(
	const std::string& configuration, const std::string& targetGlob) const
{
	const auto indexPath{findIndexFile()};
	if (indexPath.empty())
	{
		LOG_WARNING("CMakeFileAPIReader: no reply index found in " + m_replyDir.str());
		return {};
	}

	const auto indexDoc{readJsonFile(indexPath)};
	if (indexDoc.isNull())
		return {};

	// Find the codemodel-v2 reply filename via JSONPath.
	const auto pathResult{
		JSONPath::create(u"$.objects[?(@.kind == \"codemodel\")].jsonFile")};
	if (!pathResult)
	{
		LOG_ERROR(
			"CMakeFileAPIReader: JSONPath parse error: " +
			pathResult.error().formatted_message().toStdString());
		return {};
	}
	const auto codemodelFiles{pathResult->evaluate(indexDoc)};
	if (!codemodelFiles || codemodelFiles->isEmpty())
	{
		LOG_WARNING("CMakeFileAPIReader: no codemodel-v2 entry in reply index");
		return {};
	}
	const auto codemodelFilename{codemodelFiles->first().toString().toStdString()};
	const auto codemodelPath{m_replyDir.getConcatenated("/" + codemodelFilename)};
	const auto codemodelDoc{readJsonFile(codemodelPath)};
	if (codemodelDoc.isNull())
		return {};

	// Validate the codemodel document against our schema.
	const auto schema{JSONSchema::create(QJsonValue{k_codemodelSchema})};
	if (!schema)
	{
		LOG_ERROR(
			"CMakeFileAPIReader: schema compile error: " +
			schema.error().formatted_message().toStdString());
		return {};
	}
	const auto validation{schema->validate(codemodelDoc.object())};
	if (!validation.isValid())
	{
		for (const auto& err : validation.errors())
			LOG_WARNING(
				"CMakeFileAPIReader: codemodel schema violation: " +
				err.message.toStdString());
		return {};
	}

	// The codemodel top-level paths.source is the absolute source directory;
	// relative source file paths in target replies are relative to it.
	const FilePath sourceDir{
		codemodelDoc.object()["paths"].toObject()["source"].toString().toStdString()};

	// Find the toolchains-v1 reply filename via JSONPath.
	const auto toolchainsPathResult{
		JSONPath::create(u"$.objects[?(@.kind == \"toolchains\")].jsonFile")};
	QJsonObject toolchainsDocObject{};
	if (toolchainsPathResult)
	{
		if (const auto toolchainsFiles = toolchainsPathResult->evaluate(indexDoc); toolchainsFiles && !toolchainsFiles->isEmpty())
		{
			const auto toolchainsFilename{toolchainsFiles->first().toString().toStdString()};
			const auto toolchainsPath{m_replyDir.getConcatenated("/" + toolchainsFilename)};
			const auto toolchainsDoc{readJsonFile(toolchainsPath)};
			if (!toolchainsDoc.isNull())
				toolchainsDocObject = toolchainsDoc.object();
		}
	}

	// Pick the configuration to use.
	const auto configs{codemodelDoc.object()["configurations"].toArray()};
	QJsonObject chosenConfig{};
	for (const auto& c : configs)
	{
		const auto name{c.toObject()["name"].toString().toStdString()};
		if (configuration.empty() || name == configuration)
		{
			chosenConfig = c.toObject();
			break;
		}
	}
	if (chosenConfig.isEmpty())
	{
		LOG_WARNING("CMakeFileAPIReader: configuration '" + configuration + "' not found");
		return {};
	}

	// Collect sources from each target.
	std::vector<SourceEntry> result{};
	const auto targets{chosenConfig["targets"].toArray()};
	for (const auto& tRef : targets)
	{
		const auto tObj{tRef.toObject()};
		const auto targetName{tObj["name"].toString().toStdString()};
		if (!matchesGlob(targetName, targetGlob))
			continue;

		const auto targetFilename{tObj["jsonFile"].toString().toStdString()};
		const auto targetPath{m_replyDir.getConcatenated("/" + targetFilename)};
		const auto targetDoc{readJsonFile(targetPath)};
		if (targetDoc.isNull())
			continue;

		const auto targetObj{targetDoc.object()};
		const auto targetType{targetObj["type"].toString().toStdString()};

		// Build compile groups: index → CompileGroup.
		std::vector<CompileGroup> compileGroups{};
		for (const auto& cgVal : targetObj["compileGroups"].toArray())
		{
			const auto cg{cgVal.toObject()};
			CompileGroup group{};
			group.language = cg["language"].toString().toStdString();

			if (!toolchainsDocObject.isEmpty())
			{
				const auto toolchains{toolchainsDocObject["toolchains"].toArray()};
				for (const auto& tcVal : toolchains)
				{
					const auto tc{tcVal.toObject()};
					if (tc["language"].toString().toStdString() == group.language)
					{
						const auto compiler{tc["compiler"].toObject()};
						group.compilerPath = compiler["path"].toString().toStdString();

						const auto implicitObj{compiler["implicit"].toObject()};
						if (implicitObj.contains("sysroot"))
						{
							group.sysroot = FilePath(implicitObj["sysroot"].toString().toStdString());
						}
						// If CMake parsed an implicit sysroot from the compiler, expose it here
						// (typically only present if cross-compiling or if macOS with SDKROOT set)
						// Otherwise we'll have to inject it later based on the host OS
						break;
					}
				}
			}

			for (const auto& incVal : cg["includes"].toArray())
			{
				const auto inc{incVal.toObject()};
				const auto incPath{FilePath{inc["path"].toString().toStdString()}};
				if (inc["isSystem"].toBool(false))
					group.systemIncludes.push_back(incPath);
				else
					group.includes.push_back(incPath);
			}

			for (const auto& frameworkVal : cg["frameworks"].toArray())
			{
				const auto frameworkObj{frameworkVal.toObject()};
				const QString frameworkPathString{frameworkObj["path"].toString()};
				if (frameworkPathString.isEmpty())
					continue;

				FilePath frameworkSearchPath{frameworkPathString.toStdString()};
				if (frameworkPathString.endsWith(".framework", Qt::CaseInsensitive))
					frameworkSearchPath = frameworkSearchPath.getParentDirectory();

				group.frameworkSearchPaths.push_back(frameworkSearchPath);
			}

			for (const auto& defVal : cg["defines"].toArray())
				group.defines.push_back(defVal.toObject()["define"].toString().toStdString());

			for (const auto& fragVal : cg["compileCommandFragments"].toArray())
			{
				const QString fragment = fragVal.toObject()["fragment"].toString().trimmed();
				if (fragment.isEmpty())
					continue;

				const QStringList tokens = QProcess::splitCommand(fragment);
				for (const QString& token : tokens)
					group.compileFlags.push_back(token.toStdString());
			}

			compileGroups.push_back(std::move(group));
		}

		auto appendSourceEntry = [&](const QJsonValue& srcVal) {
			const auto src{srcVal.toObject()};
			const QString rawPathString{
				srcVal.isString() ? srcVal.toString() : src["path"].toString()};
			if (rawPathString.isEmpty())
				return;

			SourceEntry entry{};
			const auto rawPath{rawPathString.toStdString()};
			// Per CMake File API spec, relative paths are relative to the source dir.
			entry.path = FilePath{rawPath}.isAbsolute()
				? FilePath{rawPath}
				: sourceDir.getConcatenated("/" + rawPath);
			entry.isGenerated = src["isGenerated"].toBool(false);
			entry.targetName = targetName;
			entry.targetType = targetType;

			const auto cgIdx{src["compileGroupIndex"]};
			if (!cgIdx.isUndefined())
			{
				const auto idx{static_cast<std::size_t>(cgIdx.toInt())};
				if (idx < compileGroups.size())
					entry.compileGroup = compileGroups[idx];
			}

			for (const auto& existingEntry : result)
				if (existingEntry.targetName == entry.targetName &&
					existingEntry.path.str() == entry.path.str())
					return;

			result.push_back(std::move(entry));
		};

		// Map regular target sources to their compile groups.
		for (const auto& srcVal : targetObj["sources"].toArray())
			appendSourceEntry(srcVal);

		// CMake can expose C++20 module units via FILE_SET TYPE CXX_MODULES.
		for (const auto& fileSetVal : targetObj["fileSets"].toArray())
		{
			const auto fileSet{fileSetVal.toObject()};
			if (fileSet["type"].toString() != "CXX_MODULES")
				continue;
			for (const auto& srcVal : fileSet["sources"].toArray())
				appendSourceEntry(srcVal);
		}
	}

	return result;
}

bool CMakeFileAPIReader::isReplyStale() const
{
	const FilePath indexFile{findIndexFile()};
	if (indexFile.empty())
		return false;

	const auto indexMtime{std::filesystem::last_write_time(indexFile.getPath())};

	for (const FilePath& input : getCMakeInputFiles())
	{
		if (!input.exists())
			continue;
		if (std::filesystem::last_write_time(input.getPath()) > indexMtime)
			return true;
	}
	return false;
}

std::vector<FilePath> CMakeFileAPIReader::getCMakeInputFiles() const
{
	const auto indexPath{findIndexFile()};
	if (indexPath.empty())
		return {};

	const auto indexDoc{readJsonFile(indexPath)};
	if (indexDoc.isNull())
		return {};

	// Find the cmakeFiles-v1 reply filename.
	const auto pathResult{
		JSONPath::create(u"$.objects[?(@.kind == \"cmakeFiles\")].jsonFile")};
	if (!pathResult)
		return {};

	const auto files{pathResult->evaluate(indexDoc)};
	if (!files || files->isEmpty())
		return {};

	const auto cmakeFilesPath{
		m_replyDir.getConcatenated("/" + files->first().toString().toStdString())};
	const auto cmakeFilesDoc{readJsonFile(cmakeFilesPath)};
	if (cmakeFilesDoc.isNull())
		return {};

	// Relative paths in the cmakeFiles reply are relative to paths.source.
	const FilePath sourceDir{
		cmakeFilesDoc.object()["paths"].toObject()["source"].toString().toStdString()};
	const FilePath& baseDir{sourceDir.empty() ? m_buildDir : sourceDir};

	// Extract all input file paths using JSONPath.
	const auto inputsPath{JSONPath::create(u"$.inputs[*].path")};
	if (!inputsPath)
		return {};

	const auto inputs{inputsPath->evaluate(cmakeFilesDoc)};
	if (!inputs)
		return {};

	std::vector<FilePath> result{};
	result.reserve(static_cast<std::size_t>(inputs->size()));
	for (const auto& v : *inputs)
	{
		const auto rawPath{v.toString().toStdString()};
		result.push_back(
			FilePath{rawPath}.isAbsolute() ? FilePath{rawPath}
										   : baseDir.getConcatenated("/" + rawPath));
	}
	return result;
}
