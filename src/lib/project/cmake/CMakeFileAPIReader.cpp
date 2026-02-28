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

#include <nlohmann/json.hpp>

#include <sstream>

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

nlohmann::json toNlohmannJson(const QJsonValue& value);

nlohmann::json toNlohmannJson(const QJsonObject& object)
{
	nlohmann::json result = nlohmann::json::object();
	for (auto it = object.begin(); it != object.end(); ++it)
		result[it.key().toStdString()] = toNlohmannJson(it.value());
	return result;
}

nlohmann::json toNlohmannJson(const QJsonArray& array)
{
	nlohmann::json result = nlohmann::json::array();
	for (const auto& entry : array)
		result.push_back(toNlohmannJson(entry));
	return result;
}

nlohmann::json toNlohmannJson(const QJsonValue& value)
{
	switch (value.type())
	{
	case QJsonValue::Null:
	case QJsonValue::Undefined:
		return nullptr;
	case QJsonValue::Bool:
		return value.toBool();
	case QJsonValue::Double:
		return value.toDouble();
	case QJsonValue::String:
		return value.toString().toStdString();
	case QJsonValue::Array:
		return toNlohmannJson(value.toArray());
	case QJsonValue::Object:
		return toNlohmannJson(value.toObject());
	}
	return nullptr;
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

std::vector<CMakeFileAPIReader::JsonEntryPoint> CMakeFileAPIReader::getJsonEntryPoints() const
{
	const FilePath indexPath{findIndexFile()};
	if (indexPath.empty())
		return {};

	const QJsonDocument indexDoc{readJsonFile(indexPath)};
	if (indexDoc.isNull())
		return {};

	std::vector<JsonEntryPoint> result{};
	for (const QJsonValue& objectValue : indexDoc.object()["objects"].toArray())
	{
		const QJsonObject object{objectValue.toObject()};
		const std::string jsonFile{object["jsonFile"].toString().toStdString()};
		if (jsonFile.empty())
			continue;

		result.push_back(
			{object["kind"].toString().toStdString(), m_replyDir.getConcatenated("/" + jsonFile)});
	}

	return result;
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

CMakeFileAPIReader::GetSourcesExpected CMakeFileAPIReader::getSourcesDetailed(
	const std::string& configuration, const std::string& targetGlob) const
{
	GetSourcesResult detailedResult{};
	const auto indexPath{findIndexFile()};
	if (indexPath.empty())
	{
		LOG_WARNING("CMakeFileAPIReader: no reply index found in " + m_replyDir.str());
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::ReplyIndexNotFound,
			"No CMake File API index found in " + m_replyDir.str()));
	}

	// Read the codemodel bytes before parsing any other JSON document.
	// Qt's CBOR pool state is sensitive to parse order: parsing the small index
	// document first leaves the pool in a state where QJsonValue::type() returns
	// Array instead of Object for elements of large (160KB+) documents parsed
	// afterwards.  Reading raw bytes first and deferring parse until after the
	// index doc is destroyed avoids the corruption.
	QByteArray codemodelBytes{};
	QByteArray toolchainsBytes{};
	{
		// Parse index in a nested scope so indexDoc is destroyed before we
		// parse the codemodel, releasing its CBOR nodes back to the pool.
		const auto indexDoc{readJsonFile(indexPath)};
		if (indexDoc.isNull())
		{
			return std::unexpected(utility::makeExpectedError(
				GetSourcesErrorCode::ReplyIndexUnreadable,
				"Failed to read CMake File API index: " + indexPath.str()));
		}

		std::string codemodelFilename{};
		std::string toolchainsFilename{};
		for (const auto& obj : indexDoc.object()["objects"].toArray())
		{
			const auto kind{obj.toObject()["kind"].toString()};
			const auto file{obj.toObject()["jsonFile"].toString().toStdString()};
			if (kind == QLatin1String("codemodel"))
				codemodelFilename = file;
			else if (kind == QLatin1String("toolchains"))
				toolchainsFilename = file;
		}
		if (codemodelFilename.empty())
		{
			LOG_WARNING("CMakeFileAPIReader: no codemodel-v2 entry in reply index");
			return std::unexpected(utility::makeExpectedError(
				GetSourcesErrorCode::CodemodelReferenceMissing,
				"No codemodel-v2 entry in CMake File API index: " + indexPath.str()));
		}

		const auto readBytes = [](const FilePath& path) -> QByteArray
		{
			QFile f{QString::fromStdString(path.str())};
			if (!f.open(QIODevice::ReadOnly))
				return {};
			return f.readAll();
		};

		codemodelBytes = readBytes(m_replyDir.getConcatenated("/" + codemodelFilename));
		if (!toolchainsFilename.empty())
			toolchainsBytes = readBytes(m_replyDir.getConcatenated("/" + toolchainsFilename));
	}
	// indexDoc is now destroyed.  Parse codemodel with nlohmann/json to bypass
	// Qt's CBOR pool, which misreports QJsonValue::type() in the LLVM clang
	// build due to an interaction between Homebrew LLVM 21 and Qt's internal
	// CBOR allocator.  nlohmann/json has no such shared state.
	if (codemodelBytes.isEmpty())
	{
		LOG_WARNING("CMakeFileAPIReader: failed to read codemodel bytes");
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::CodemodelUnreadable,
			"Failed to read codemodel bytes from CMake File API reply in " + m_replyDir.str()));
	}
	nlohmann::json codemodelNlohmann;
	try
	{
		const std::string codemodelText{
			codemodelBytes.constData(), static_cast<std::size_t>(codemodelBytes.size())};
		codemodelNlohmann = nlohmann::json::parse(codemodelText);
	}
	catch (const nlohmann::json::parse_error& e)
	{
		LOG_WARNING("CMakeFileAPIReader: codemodel parse error: " + std::string(e.what()));
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::CodemodelParseError,
			"Failed to parse codemodel JSON: " + std::string(e.what())));
	}
	if (!codemodelNlohmann.is_object())
	{
		LOG_WARNING("CMakeFileAPIReader: codemodel root is not an object");
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::CodemodelRootNotObject,
			"Codemodel root is not a JSON object"));
	}

	// Structural validation.
	const auto kind{codemodelNlohmann.value("kind", std::string{})};
	const int  major{codemodelNlohmann.value("version", nlohmann::json::object())
		.value("major", 0)};
	if (kind != "codemodel" || major < 2)
	{
		LOG_WARNING("CMakeFileAPIReader: unexpected codemodel kind='" + kind
			+ "' major=" + std::to_string(major));
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::CodemodelUnexpectedSchema,
			"Unexpected codemodel schema: kind='" + kind +
			"' major=" + std::to_string(major)));
	}

	const FilePath sourceDir{
		codemodelNlohmann.value("paths", nlohmann::json::object())
			.value("source", std::string{})};

	// Parse toolchains (also bypassing Qt JSON).
	nlohmann::json toolchainsNl;
	if (!toolchainsBytes.isEmpty())
	{
		try
		{
			const std::string toolchainsText{
				toolchainsBytes.constData(), static_cast<std::size_t>(toolchainsBytes.size())};
			toolchainsNl = nlohmann::json::parse(toolchainsText);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			LOG_WARNING("CMakeFileAPIReader: toolchains parse error: " + std::string(e.what()));
		}
	}

	// Pick the configuration.
	static const nlohmann::json s_emptyArray = nlohmann::json::array();
	const auto& configsNl = codemodelNlohmann.contains("configurations")
		? codemodelNlohmann.at("configurations")
		: s_emptyArray;
	nlohmann::json chosenConfigNl{};
	bool hasChosenConfig{false};
	std::vector<std::string> configNames{};
	for (const auto& c : configsNl)
	{
		const auto name{c.value("name", std::string{})};
		configNames.push_back(name.empty() ? "<empty>" : name);
		if (!hasChosenConfig && (configuration.empty() || name == configuration))
		{
			chosenConfigNl = c;
			hasChosenConfig = true;
		}
	}
	LOG_INFO(
		"CMakeFileAPIReader: getSources configuration='" + configuration +
		"' targetGlob='" + targetGlob +
		"' configurations=" + std::to_string(configNames.size()));
	if (!hasChosenConfig)
	{
		std::string availableConfigurations{};
		for (std::size_t i{0}; i < configNames.size(); ++i)
		{
			if (i > 0)
				availableConfigurations += ", ";
			availableConfigurations += configNames[i];
		}
		LOG_WARNING(
			"CMakeFileAPIReader: configuration '" + configuration +
			"' not found. available: [" + availableConfigurations + "]");
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::ConfigurationNotFound,
			"CMake configuration '" + configuration + "' not found. available: [" +
				availableConfigurations + "]"));
	}
	const auto chosenConfigurationName{chosenConfigNl.value("name", std::string{})};
	const auto targetsNl{chosenConfigNl.value("targets", nlohmann::json::array())};
	LOG_INFO(
		"CMakeFileAPIReader: selected configuration '" + chosenConfigurationName +
		"' with " + std::to_string(targetsNl.size()) + " targets");

	const auto parseJsonBytes = [](const QByteArray& bytes) -> nlohmann::json
	{
		const std::string jsonText{bytes.constData(), static_cast<std::size_t>(bytes.size())};
		return nlohmann::json::parse(jsonText);
	};

	// Helper: read a file to bytes and parse with nlohmann.
	const auto readNlohmann = [&](const FilePath& path) -> nlohmann::json
	{
		QFile f{QString::fromStdString(path.str())};
		if (!f.open(QIODevice::ReadOnly))
		{
			LOG_WARNING("CMakeFileAPIReader: cannot open target JSON file: " + path.str());
			return {};
		}
		const QByteArray bytes{f.readAll()};
		if (bytes.isEmpty())
		{
			LOG_WARNING("CMakeFileAPIReader: empty target JSON file: " + path.str());
			return {};
		}

		QJsonParseError qtParseError{};
		const auto qtDoc{QJsonDocument::fromJson(bytes, &qtParseError)};
		if (qtParseError.error == QJsonParseError::NoError)
		{
			if (qtDoc.isObject())
				return toNlohmannJson(qtDoc.object());

			if (qtDoc.isArray())
			{
				const auto qtArray{qtDoc.array()};
				if (qtArray.size() == 1 && qtArray.first().isObject())
				{
					LOG_WARNING(
						"CMakeFileAPIReader: normalized array-wrapped target JSON: " + path.str());
					return toNlohmannJson(qtArray.first().toObject());
				}

				LOG_WARNING(
					"CMakeFileAPIReader: Qt parsed target JSON as array in " + path.str() +
					" (size=" + std::to_string(qtArray.size()) + ")");
				return toNlohmannJson(qtArray);
			}
		}
		try
		{
			const auto parsed{parseJsonBytes(bytes)};
			if (!parsed.is_object())
			{
				if (parsed.is_array())
				{
					if (parsed.size() == 1 && parsed[0].is_object())
					{
						LOG_WARNING(
							"CMakeFileAPIReader: normalized nlohmann array-wrapped target JSON: " +
							path.str());
						return parsed[0];
					}

					bool looksLikeKeyValuePairs{!parsed.empty()};
					for (const auto& entry : parsed)
					{
						if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string())
						{
							looksLikeKeyValuePairs = false;
							break;
						}
					}
					if (looksLikeKeyValuePairs)
					{
						nlohmann::json normalizedObject = nlohmann::json::object();
						for (const auto& entry : parsed)
							normalizedObject[entry[0].get<std::string>()] = entry[1];

						LOG_WARNING(
							"CMakeFileAPIReader: normalized nlohmann key/value target JSON: " +
							path.str());
						return normalizedObject;
					}
				}

				static bool s_loggedNonObjectTargetPreview{false};
				if (!s_loggedNonObjectTargetPreview)
				{
					s_loggedNonObjectTargetPreview = true;

					char firstNonWhitespace{'?'};
					for (char c : bytes)
					{
						if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
							continue;
						firstNonWhitespace = c;
						break;
					}

					const int previewLength{std::min<int>(bytes.size(), 96)};
					QString preview{
						QString::fromUtf8(bytes.constData(), previewLength)};
					preview.replace("\n", "\\n");
					preview.replace("\r", "\\r");
					preview.replace("\t", "\\t");

					LOG_WARNING(
						"CMakeFileAPIReader: non-object target parse sample: " + path.str() +
						" first_non_ws='" + std::string(1, firstNonWhitespace) +
						"' preview='" + preview.toStdString() + "'");
				}
			}
			return parsed;
		}
		catch (const std::exception& e)
		{
			LOG_WARNING(
				"CMakeFileAPIReader: parse error in " + path.str() +
				" (bytes=" + std::to_string(bytes.size()) + "): " + std::string(e.what()));
			return {};
		}
	};

	// Collect sources from each target.
	auto& result{detailedResult.entries};
	auto& warnings{detailedResult.warnings};
	std::vector<nlohmann::json> normalizedTargetReferences{};
	std::size_t nestedTargetReferenceArrayCount{0};
	for (const auto& targetReference : targetsNl)
	{
		if (!targetReference.is_array())
		{
			normalizedTargetReferences.push_back(targetReference);
			continue;
		}

		++nestedTargetReferenceArrayCount;
		for (const auto& nestedTargetReference : targetReference)
			normalizedTargetReferences.push_back(nestedTargetReference);
	}
	if (nestedTargetReferenceArrayCount > 0)
	{
		LOG_WARNING(
			"CMakeFileAPIReader: flattened " + std::to_string(nestedTargetReferenceArrayCount) +
			" nested target reference arrays into " +
			std::to_string(normalizedTargetReferences.size()) + " target references");
		warnings.push_back(
			{GetSourcesWarningCode::NestedTargetReferenceArraysFlattened, {}, m_replyDir});
	}

	std::size_t matchedTargetCount{0};
	std::size_t malformedTargetReferenceCount{0};
	std::size_t emptyTargetReplyCount{0};
	std::size_t unreadableTargetReplyCount{0};
	std::size_t sourceObjectCount{0};
	std::size_t duplicateSourceCount{0};
	for (const auto& tRef : normalizedTargetReferences)
	{
		if (!tRef.is_object())
		{
			++malformedTargetReferenceCount;
			LOG_WARNING(
				"CMakeFileAPIReader: non-object target reference type='" +
				std::string{tRef.type_name()} + "'");
			warnings.push_back({GetSourcesWarningCode::MalformedTargetReference, {}, {}});
			continue;
		}

		const auto targetName{tRef.value("name", std::string{})};
		if (!matchesGlob(targetName, targetGlob))
			continue;
		++matchedTargetCount;

		const auto targetFilename{tRef.value("jsonFile", std::string{})};
		if (targetFilename.empty())
		{
			++emptyTargetReplyCount;
			LOG_WARNING(
				"CMakeFileAPIReader: target '" + targetName +
				"' has no jsonFile in codemodel");
			warnings.push_back({GetSourcesWarningCode::TargetMissingJsonFile, targetName, {}});
			continue;
		}
		const FilePath targetReplyPath{m_replyDir.getConcatenated("/" + targetFilename)};
		const auto targetNlRaw{readNlohmann(targetReplyPath)};
		nlohmann::json normalizedTargetNl{};
		const nlohmann::json* targetNl{&targetNlRaw};

		if (targetNlRaw.is_array() && targetNlRaw.size() == 1 && targetNlRaw[0].is_object())
		{
			normalizedTargetNl = targetNlRaw[0];
			targetNl = &normalizedTargetNl;
			LOG_WARNING(
				"CMakeFileAPIReader: normalized target root array for '" + targetName +
				"' from " + targetReplyPath.str());
			warnings.push_back(
				{GetSourcesWarningCode::TargetRootArrayNormalized, targetName, targetReplyPath});
		}
		else if (targetNlRaw.is_array())
		{
			bool looksLikeKeyValuePairs{!targetNlRaw.empty()};
			for (const auto& entry : targetNlRaw)
			{
				if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string())
				{
					looksLikeKeyValuePairs = false;
					break;
				}
			}

			if (looksLikeKeyValuePairs)
			{
				normalizedTargetNl = nlohmann::json::object();
				for (const auto& entry : targetNlRaw)
					normalizedTargetNl[entry[0].get<std::string>()] = entry[1];
				targetNl = &normalizedTargetNl;
				LOG_WARNING(
					"CMakeFileAPIReader: normalized target key/value array for '" + targetName +
					"' from " + targetReplyPath.str());
				warnings.push_back(
					{GetSourcesWarningCode::TargetKeyValueArrayNormalized, targetName, targetReplyPath});
			}
		}

		if (targetNl->is_null() || !targetNl->is_object())
		{
			++unreadableTargetReplyCount;
			const std::string targetArraySize{targetNl->is_array()
				? std::to_string(targetNl->size())
				: std::string{"n/a"}};
			const std::string firstArrayElementType{targetNl->is_array() && !targetNl->empty()
				? std::string{(*targetNl)[0].type_name()}
				: std::string{"n/a"}};
			LOG_WARNING(
				"CMakeFileAPIReader: invalid target JSON for '" + targetName +
				"': " + targetReplyPath.str() +
				" type='" + targetNl->type_name() + "'" +
				" is_null=" + std::to_string(targetNl->is_null()) +
				" is_object=" + std::to_string(targetNl->is_object()) +
				" is_discarded=" + std::to_string(targetNl->is_discarded()) +
				" array_size=" + targetArraySize +
				" first_array_element_type='" + firstArrayElementType + "'");
			warnings.push_back(
				{GetSourcesWarningCode::TargetReplyUnreadable, targetName, targetReplyPath});
			continue;
		}

		const auto targetType{targetNl->value("type", std::string{})};

		// Build compile groups: index → CompileGroup.
		std::vector<CompileGroup> compileGroups{};
		for (const auto& cg : targetNl->value("compileGroups", nlohmann::json::array()))
		{
			CompileGroup group{};
			group.language = cg.value("language", std::string{});

			if (!toolchainsNl.is_null())
			{
				for (const auto& tc : toolchainsNl.value("toolchains", nlohmann::json::array()))
				{
					if (tc.value("language", std::string{}) != group.language)
						continue;
					const auto& compiler{tc.value("compiler", nlohmann::json::object())};
					group.compilerPath = compiler.value("path", std::string{});
					const auto& implicit{compiler.value("implicit", nlohmann::json::object())};
					const auto sysroot{implicit.value("sysroot", std::string{})};
					if (!sysroot.empty())
						group.sysroot = FilePath{sysroot};
					break;
				}
			}

			for (const auto& inc : cg.value("includes", nlohmann::json::array()))
			{
				const FilePath incPath{inc.value("path", std::string{})};
				if (inc.value("isSystem", false))
					group.systemIncludes.push_back(incPath);
				else
					group.includes.push_back(incPath);
			}

			for (const auto& fw : cg.value("frameworks", nlohmann::json::array()))
			{
				const std::string fwPath{fw.value("path", std::string{})};
				if (fwPath.empty())
					continue;
				FilePath fwSearchPath{fwPath};
				if (QString::fromStdString(fwPath).endsWith(".framework", Qt::CaseInsensitive))
					fwSearchPath = fwSearchPath.getParentDirectory();
				group.frameworkSearchPaths.push_back(fwSearchPath);
			}

			for (const auto& def : cg.value("defines", nlohmann::json::array()))
				group.defines.push_back(def.value("define", std::string{}));

			for (const auto& frag : cg.value("compileCommandFragments", nlohmann::json::array()))
			{
				const QString fragment =
					QString::fromStdString(frag.value("fragment", std::string{})).trimmed();
				if (fragment.isEmpty())
					continue;
				for (const QString& token : QProcess::splitCommand(fragment))
					group.compileFlags.push_back(token.toStdString());
			}

			compileGroups.push_back(std::move(group));
		}

		auto appendSourceEntry = [&](const nlohmann::json& src)
		{
			++sourceObjectCount;
			const std::string rawPath{src.is_string()
				? src.get<std::string>()
				: src.value("path", std::string{})};
			if (rawPath.empty())
				return;

			SourceEntry entry{};
			entry.path = FilePath{rawPath}.isAbsolute()
				? FilePath{rawPath}
				: sourceDir.getConcatenated("/" + rawPath);
			entry.isGenerated = src.value("isGenerated", false);
			entry.targetName = targetName;
			entry.targetType = targetType;
			entry.sourceDir = sourceDir;

			if (src.contains("compileGroupIndex"))
			{
				const auto idx{static_cast<std::size_t>(src["compileGroupIndex"].get<int>())};
				if (idx < compileGroups.size())
					entry.compileGroup = compileGroups[idx];
			}

			for (const auto& existingEntry : result)
				if (existingEntry.targetName == entry.targetName &&
					existingEntry.path.str() == entry.path.str())
				{
					++duplicateSourceCount;
					return;
				}

			result.push_back(std::move(entry));
		};

		for (const auto& src : targetNl->value("sources", nlohmann::json::array()))
			appendSourceEntry(src);

		for (const auto& fileSet : targetNl->value("fileSets", nlohmann::json::array()))
		{
			if (fileSet.value("type", std::string{}) != "CXX_MODULES")
				continue;
			for (const auto& src : fileSet.value("sources", nlohmann::json::array()))
				appendSourceEntry(src);
		}
	}

	LOG_INFO(
		"CMakeFileAPIReader: getSources summary targets=" + std::to_string(targetsNl.size()) +
		", normalized_targets=" + std::to_string(normalizedTargetReferences.size()) +
		", nested_target_ref_arrays=" + std::to_string(nestedTargetReferenceArrayCount) +
		", matched=" + std::to_string(matchedTargetCount) +
		", malformed_target_ref=" + std::to_string(malformedTargetReferenceCount) +
		", empty_reply=" + std::to_string(emptyTargetReplyCount) +
		", unreadable_reply=" + std::to_string(unreadableTargetReplyCount) +
		", source_objects=" + std::to_string(sourceObjectCount) +
		", duplicates=" + std::to_string(duplicateSourceCount) +
		", result_entries=" + std::to_string(result.size()));

	detailedResult.targetCount = targetsNl.size();
	detailedResult.normalizedTargetCount = normalizedTargetReferences.size();
	detailedResult.matchedTargetCount = matchedTargetCount;
	detailedResult.malformedTargetReferenceCount = malformedTargetReferenceCount;
	detailedResult.emptyTargetReplyCount = emptyTargetReplyCount;
	detailedResult.unreadableTargetReplyCount = unreadableTargetReplyCount;
	detailedResult.sourceObjectCount = sourceObjectCount;
	detailedResult.duplicateSourceCount = duplicateSourceCount;

	if (matchedTargetCount > 0 && unreadableTargetReplyCount == matchedTargetCount)
	{
		return std::unexpected(utility::makeExpectedError(
			GetSourcesErrorCode::AllMatchedTargetsUnreadable,
			"All matched target replies were unreadable for configuration '" +
				chosenConfigurationName + "'"));
	}

	return detailedResult;
}

std::vector<CMakeFileAPIReader::SourceEntry> CMakeFileAPIReader::getSources(
	const std::string& configuration, const std::string& targetGlob) const
{
	const auto sourcesResult{getSourcesDetailed(configuration, targetGlob)};
	if (sourcesResult.has_value())
		return sourcesResult->entries;

	const auto& error{sourcesResult.error()};
	LOG_WARNING(
		"CMakeFileAPIReader: getSources failed code='" +
		getSourcesErrorCodeToString(error.code) + "' message='" + error.message + "'");
	return {};
}

std::string CMakeFileAPIReader::getSourcesErrorCodeToString(const GetSourcesErrorCode& code)
{
	switch (code)
	{
	case GetSourcesErrorCode::ReplyIndexNotFound:
		return "reply_index_not_found";
	case GetSourcesErrorCode::ReplyIndexUnreadable:
		return "reply_index_unreadable";
	case GetSourcesErrorCode::CodemodelReferenceMissing:
		return "codemodel_reference_missing";
	case GetSourcesErrorCode::CodemodelUnreadable:
		return "codemodel_unreadable";
	case GetSourcesErrorCode::CodemodelParseError:
		return "codemodel_parse_error";
	case GetSourcesErrorCode::CodemodelRootNotObject:
		return "codemodel_root_not_object";
	case GetSourcesErrorCode::CodemodelUnexpectedSchema:
		return "codemodel_unexpected_schema";
	case GetSourcesErrorCode::ConfigurationNotFound:
		return "configuration_not_found";
	case GetSourcesErrorCode::AllMatchedTargetsUnreadable:
		return "all_matched_targets_unreadable";
	}
	return "unknown";
}

std::string CMakeFileAPIReader::getSourcesWarningCodeToString(const GetSourcesWarningCode& code)
{
	switch (code)
	{
	case GetSourcesWarningCode::NestedTargetReferenceArraysFlattened:
		return "nested_target_reference_arrays_flattened";
	case GetSourcesWarningCode::MalformedTargetReference:
		return "malformed_target_reference";
	case GetSourcesWarningCode::TargetMissingJsonFile:
		return "target_missing_json_file";
	case GetSourcesWarningCode::TargetRootArrayNormalized:
		return "target_root_array_normalized";
	case GetSourcesWarningCode::TargetKeyValueArrayNormalized:
		return "target_key_value_array_normalized";
	case GetSourcesWarningCode::TargetReplyUnreadable:
		return "target_reply_unreadable";
	}
	return "unknown";
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

	const auto readNlohmannFile = [](const FilePath& path) -> nlohmann::json
	{
		QFile file{QString::fromStdString(path.str())};
		if (!file.open(QIODevice::ReadOnly))
			return {};

		const QByteArray bytes{file.readAll()};
		if (bytes.isEmpty())
			return {};

		try
		{
			const std::string jsonText{bytes.constData(), static_cast<std::size_t>(bytes.size())};
			return nlohmann::json::parse(jsonText);
		}
		catch (const std::exception&)
		{
			return {};
		}
	};

	const auto indexNl{readNlohmannFile(indexPath)};
	if (!indexNl.is_object())
		return {};

	std::string cmakeFilesFilename{};
	for (const auto& obj : indexNl.value("objects", nlohmann::json::array()))
	{
		if (!obj.is_object())
			continue;
		if (obj.value("kind", std::string{}) != "cmakeFiles")
			continue;
		cmakeFilesFilename = obj.value("jsonFile", std::string{});
		if (!cmakeFilesFilename.empty())
			break;
	}
	if (cmakeFilesFilename.empty())
		return {};

	const auto cmakeFilesPath{m_replyDir.getConcatenated("/" + cmakeFilesFilename)};
	const auto cmakeFilesNl{readNlohmannFile(cmakeFilesPath)};
	if (!cmakeFilesNl.is_object())
		return {};

	const FilePath sourceDir{
		cmakeFilesNl.value("paths", nlohmann::json::object()).value("source", std::string{})};
	const FilePath& baseDir{sourceDir.empty() ? m_buildDir : sourceDir};

	std::vector<FilePath> result{};
	for (const auto& input : cmakeFilesNl.value("inputs", nlohmann::json::array()))
	{
		const std::string rawPath{input.is_string()
			? input.get<std::string>()
			: input.value("path", std::string{})};
		if (rawPath.empty())
			continue;

		result.push_back(
			FilePath{rawPath}.isAbsolute() ? FilePath{rawPath}
									   : baseDir.getConcatenated("/" + rawPath));
	}
	return result;
}
