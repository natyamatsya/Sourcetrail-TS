#ifndef COLOR_SCHEME_H
#define COLOR_SCHEME_H

#include "SrctrlModule.h"

// Family-internal include (srctrl.settings), unguarded: same module either way.
#include "Settings.h"

// srctrl.data types: fwd/textual in the classic build; the purview gets them from
// `import srctrl.data` (the settings wrapper imports it for this header).
#ifndef SRCTRL_MODULE_PURVIEW
#include "Edge.h"
#include "Node.h"
#endif

SRCTRL_EXPORT class ColorScheme: public Settings
{
public:
	enum class ColorState
	{
		NORMAL,
		FOCUS,
		ACTIVE
	};

	static std::shared_ptr<ColorScheme> getInstance();
	~ColorScheme() override;

	bool hasColor(const std::string& key) const;

	std::string getColor(const std::string& key) const;
	std::string getColor(const std::string& key, const std::string& defaultColor) const;

	std::string getNodeTypeColor(NodeType type, const std::string& key, bool highlight) const;
	std::string getNodeTypeColor(const std::string& typeStr, const std::string& key, bool highlight) const;

	std::string getEdgeTypeColor(Edge::EdgeType type) const;
	std::string getEdgeTypeColor(const std::string& type) const;

	std::string getSearchTypeColor(
		const std::string& searchTypeName,
		const std::string& key,
		const std::string& state = "normal") const;
	std::string getSyntaxColor(const std::string& key) const;

	std::string getCodeAnnotationTypeColor(
		const std::string& typeStr, const std::string& key, ColorState state) const;

protected:
	ColorScheme();
	ColorScheme(const ColorScheme&) = delete;
	void operator=(const ColorScheme&) = delete;

	static std::shared_ptr<ColorScheme> s_instance;

	static std::string stateToString(ColorState state);
};

#include "ColorScheme.inl"

#endif	  // COLOR_SCHEME_H
