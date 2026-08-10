#include "CommandLineParser.h"
#include "RecipeUtils.h"
#include "Utf8Path.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

std::string OptionValue(const std::string& recipe, const std::string& name)
{
	for (const std::string& token :
		CommandLineParser::tokenizeCommandLine(recipe)) {
		const std::string prefix = "/" + name + "=";
		if (token.compare(0, prefix.size(), prefix) == 0)
			return token.substr(prefix.size());
	}
	return std::string();
}

} // namespace

int main()
{
	const std::string windowsPath =
		R"(D:\Emulation\Atari 8-bit\rc-Anna_Faris-001\Anna_Faris.png-details.png)";
	const std::vector<std::string> original = {
		"/details=" + windowsPath, "/details_layer=on", "",
		"/target=/tmp/line\nbreak\tand-tab.png"
	};
	const std::string encoded = CommandLineParser::joinTokens(original);
	Require(CommandLineParser::tokenizeCommandLine(encoded) == original,
		"quoted Windows paths and empty values must round-trip");
	const std::string legacy = "/details=\"" + windowsPath + "\"";
	Require(OptionValue(legacy, "details") == windowsPath,
		"legacy recipes with raw quoted Windows separators must remain readable");

	const std::string posixPath = R"xx(/tmp/a "quoted" folder/back\slash.png)xx";
	const std::string edited = SetRecipeOption(
		R"(/details="old mask.png" /threads=8)", "details", posixPath);
	Require(OptionValue(edited, "details") == posixPath,
		"recipe editing must preserve POSIX quotes and backslashes");
	Require(OptionValue(edited, "threads") == "8",
		"recipe editing must retain unrelated options");

	const std::string oldPrefix =
		R"(D:\Emulation\Atari 8-bit\rc-Anna_Faris-001\Anna_Faris.png)";
	const std::string newPrefix =
		R"(D:\Emulation\Atari 8-bit\rc-Anna_Faris-001\snap-001\Anna_Faris.png)";
	std::string recipe;
	recipe = SetRecipeOption(recipe, "output", oldPrefix);
	recipe = SetRecipeOption(recipe, "details", oldPrefix + "-details.png");
	recipe = SetRecipeOption(recipe, "target", oldPrefix + "-target.png");
	bool relocated = false;
	const std::string moved = RelocateRecipePathPrefix(
		recipe, oldPrefix, newPrefix, &relocated);
	Require(relocated, "snapshot relocation must report a changed recipe");
	Require(OptionValue(moved, "output") == newPrefix
		&& OptionValue(moved, "details") == newPrefix + "-details.png"
		&& OptionValue(moved, "target") == newPrefix + "-target.png",
		"snapshot relocation must move every run-owned path");

#if !defined(_WIN32)
	Require(Utf8FileName(R"(/tmp/folder/name\with\slashes.png)")
		== R"(name\with\slashes.png)",
		"POSIX filenames must retain literal backslashes");
#endif
	Require(Utf8FileName(Utf8JoinPath("folder", u8"žluťoučký.png"))
		== u8"žluťoučký.png", "UTF-8 path joins must preserve filenames");

	std::cout << "RecipeUtilsTests passed\n";
	return 0;
}
