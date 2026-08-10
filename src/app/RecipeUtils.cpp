#include "RecipeUtils.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "CommandLineParser.h"

namespace {

bool IsNamedOption(const std::string& token, const std::string& name,
	bool bare)
{
	std::string foldedToken = token;
	std::transform(foldedToken.begin(), foldedToken.end(), foldedToken.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::string foldedName = name;
	std::transform(foldedName.begin(), foldedName.end(), foldedName.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	for (const char* prefixText : {"--", "-", "/"}) {
		const std::string prefix(prefixText);
		const std::string spelling = prefix + foldedName;
		if (bare ? foldedToken == spelling
			: foldedToken.compare(0, spelling.size() + 1, spelling + "=") == 0)
			return true;
	}
	return false;
}

} // namespace

std::string SetRecipeOption(const std::string& recipe,
	const std::string& name, const std::string& value,
	std::initializer_list<std::string> aliases)
{
	const std::vector<std::string> decoded =
		CommandLineParser::tokenizeCommandLine(recipe);
	std::vector<std::string> tokens;
	bool skipOptionValue = false;
	for (const std::string& token : decoded) {
		if (skipOptionValue) {
			skipOptionValue = false;
			continue;
		}
		bool replace = IsNamedOption(token, name, false);
		bool replaceBare = IsNamedOption(token, name, true);
		for (const std::string& alias : aliases) {
			replace = replace || IsNamedOption(token, alias, false);
			replaceBare = replaceBare || IsNamedOption(token, alias, true);
		}
		if (replaceBare)
			skipOptionValue = true;
		if (!replace && !replaceBare)
			tokens.push_back(token);
	}
	tokens.push_back("/" + name + "=" + value);
	return CommandLineParser::joinTokens(tokens);
}

std::string RelocateRecipePathPrefix(const std::string& recipe,
	const std::string& from, const std::string& to, bool* relocated)
{
	std::vector<std::string> tokens =
		CommandLineParser::tokenizeCommandLine(recipe);
	bool changed = false;
	for (std::string& token : tokens) {
		const size_t prefix = token.rfind("--", 0) == 0 ? 2
			: (!token.empty() && (token[0] == '/' || token[0] == '-')) ? 1 : 0;
		const size_t equals = token.find('=', prefix);
		if (prefix == 0 || equals == std::string::npos)
			continue;
		std::string option = token.substr(prefix, equals - prefix);
		std::transform(option.begin(), option.end(), option.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (option != "output" && option != "o"
			&& option != "details" && option != "target")
			continue;
		std::string value = token.substr(equals + 1);
		if (value.compare(0, from.size(), from) != 0)
			continue;
		value.replace(0, from.size(), to);
		token = token.substr(0, equals + 1) + value;
		changed = true;
	}
	if (relocated != nullptr)
		*relocated = changed;
	return CommandLineParser::joinTokens(tokens);
}
