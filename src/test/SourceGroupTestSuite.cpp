#include "Catch2.hpp"

#include <fstream>

#include "AppPath.h"
#include "Application.h"
#include "FileSystem.h"
#include "IndexerCommandCustom.h"
#include "Platform.h"
#include "ProjectSettings.h"
#include "SourceGroupCustomCommand.h"
#include "SourceGroupSettingsCustomCommand.h"
#include "TextAccess.h"
#include "Version.h"
#include "utilityPathDetection.h"
#include "utilityString.h"

using namespace std;
using namespace utility;

namespace
{
const bool updateExpectedOutput = false;

// Some tests assume that the shared data directory is empty. Make it easier to clear/reset it
// with a RAII wrapper:
class SharedDataDirectorySwitcher {
public:
	SharedDataDirectorySwitcher(const FilePath &newDirectory)
	{
		m_oldDirectory = AppPath::getSharedDataDirectoryPath();
		
		AppPath::setSharedDataDirectoryPath(newDirectory);
	}
	
	~SharedDataDirectorySwitcher()
	{
		AppPath::setSharedDataDirectoryPath(m_oldDirectory);
	}
private:
	FilePath m_oldDirectory;
};

class ScopedApplicationInstance
{
public:
	ScopedApplicationInstance()
	{
		Application::createInstance(Version(), nullptr, nullptr);
	}

	~ScopedApplicationInstance()
	{
		Application::destroyInstance();
	}
};

static FilePath getInputDirectoryPath(const std::string& projectName)
{
	return FilePath("data/SourceGroupTestSuite/" + projectName + "/input")
		.makeAbsolute()
		.makeCanonical();
}

static FilePath getOutputDirectoryPath(const std::string& projectName)
{
	return FilePath("data/SourceGroupTestSuite/" + projectName + "/expected_output")
		.makeAbsolute()
		.makeCanonical();
}

std::string indexerCommandCustomToString(
	std::shared_ptr<const IndexerCommandCustom> indexerCommand, const FilePath& baseDirectory)
{
	std::string result;
	result += "IndexerCommandCustom\n";
	result += "\tSourceFilePath: \"" +
		indexerCommand->getSourceFilePath().getRelativeTo(baseDirectory).str() + "\"\n";
	result += "\tCustom Command: \"" + indexerCommand->getCommand() + "\"\n";
	result += "\tArguments:\n";
	for (const std::string& argument: indexerCommand->getArguments())
	{
		result += "\t\t\"" + argument + "\"\n";
	}
	return result;
}

std::string indexerCommandToString(
	std::shared_ptr<IndexerCommand> indexerCommand, const FilePath& baseDirectory)
{
	if (indexerCommand)
	{
		if (std::shared_ptr<const IndexerCommandCustom> indexerCommandCustom =
				std::dynamic_pointer_cast<const IndexerCommandCustom>(indexerCommand))
		{
			return indexerCommandCustomToString(indexerCommandCustom, baseDirectory);
		}
		return "Unsupported indexer command type: " + indexerCommandTypeToString(indexerCommand->getIndexerCommandType());
	}
	return "No IndexerCommand provided.";
}

std::shared_ptr<TextAccess> generateExpectedOutput(
	std::string projectName, std::shared_ptr<const SourceGroup> sourceGroup)
{
	const FilePath projectDataRoot = getInputDirectoryPath(projectName).makeAbsolute();

	RefreshInfo info;
	info.filesToIndex = sourceGroup->getAllSourceFilePaths();
	std::vector<std::shared_ptr<IndexerCommand>> indexerCommands = sourceGroup->getIndexerCommands(
		info);

	std::sort(
		indexerCommands.begin(),
		indexerCommands.end(),
		[](std::shared_ptr<IndexerCommand> a, std::shared_ptr<IndexerCommand> b) {
			return a->getSourceFilePath().str() < b->getSourceFilePath().str();
		});

	std::string outputString;
	for (const std::shared_ptr<IndexerCommand> &indexerCommand : indexerCommands) {
		outputString += indexerCommandToString(indexerCommand, projectDataRoot);
	}

	return TextAccess::createFromString(outputString);
}

std::string getOutputFilename()
{
	if constexpr (Platform::isWindows())
		return "output_windows.txt";
	else
		return "output_unix.txt";

}
void generateAndCompareExpectedOutput(
	std::string projectName, std::shared_ptr<const SourceGroup> sourceGroup)
{
	const std::shared_ptr<const TextAccess> output = generateExpectedOutput(projectName, sourceGroup);
	const std::string expectedOutputFileName = getOutputFilename();

	const FilePath expectedOutputFilePath =
		getOutputDirectoryPath(projectName).concatenate(expectedOutputFileName);
	if (updateExpectedOutput || !expectedOutputFilePath.exists())
	{
		std::ofstream expectedOutputFile;
		expectedOutputFile.open(expectedOutputFilePath.str());
		expectedOutputFile << output->getText();
		expectedOutputFile.close();
	}
	else
	{
		const std::shared_ptr<const TextAccess> expectedOutput = TextAccess::createFromFile(
			expectedOutputFilePath);
		REQUIRE_MESSAGE(
			("Output does not match the expected line count for project \"" +
			 projectName) + "\". Output was: " + output->getText(),
			expectedOutput->getLineCount() == output->getLineCount());
		if (expectedOutput->getLineCount() == output->getLineCount())
		{
			for (unsigned int i = 1; i <= expectedOutput->getLineCount(); i++)
			{
				REQUIRE(expectedOutput->getLine(i) == output->getLine(i));
			}
		}
	}
}
}	 // namespace

TEST_CASE("source group custom command generates expected output")
{
	ScopedApplicationInstance scopedApplicationInstance;
	SharedDataDirectorySwitcher sharedDataDirectorySwitcher((FilePath()));

	const std::string projectName = "custom_command";

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("non_existent_project", getInputDirectoryPath(projectName));

	std::shared_ptr<SourceGroupSettingsCustomCommand> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCustomCommand>("fake_id", &projectSettings);
	sourceGroupSettings->setCustomCommand("echo \"Hello World\"");
	sourceGroupSettings->setSourcePaths({getInputDirectoryPath(projectName).concatenate("/src")});
	sourceGroupSettings->setSourceExtensions({".txt"});
	sourceGroupSettings->setExcludeFilterStrings({"**/excluded/**"});

	generateAndCompareExpectedOutput(
		projectName, std::make_shared<SourceGroupCustomCommand>(sourceGroupSettings));
}
