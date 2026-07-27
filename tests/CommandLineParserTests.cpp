#include "CommandLineParser.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

CommandLineParser MakeParser()
{
	CommandLineParser parser;
	parser.addOption("input", {"i"}, "FILE", "", "Input image.", "General");
	parser.addOption("height", {"h"}, "N", "240", "Picture height.", "General");
	parser.addOption("threads", {}, "N", "1", "Worker threads.", "General");
	return parser;
}

std::vector<std::string> Parse(const std::vector<std::string>& tokens)
{
	CommandLineParser parser = MakeParser();
	parser.parseTokens(tokens);
	return parser.getPositionalArguments();
}

} // namespace

int main()
{
	// A path is a path, wherever it points. Both of these used to be rejected:
	// the parser read a leading '/' as an option prefix, and the caller threw
	// away any positional argument containing a slash, so on Linux and macOS
	// the program could only be handed a file in the current directory.
	const std::vector<std::string> relative = Parse({"photos/pic.png", "/h=40"});
	Require(relative.size() == 1 && relative[0] == "photos/pic.png",
		"a relative path with a slash must be positional, not an option");

#if !defined(_WIN32)
	const std::vector<std::string> absolute = Parse({"/home/me/pic.png", "/h=40"});
	Require(absolute.size() == 1 && absolute[0] == "/home/me/pic.png",
		"an absolute POSIX path must be positional, not an option");
#endif

	// The Windows-style spelling of a real option still is one, on every
	// platform - recipes and documentation are full of it.
	{
		CommandLineParser parser = MakeParser();
		parser.parseTokens({"pic.png", "/h=40", "/threads=8"});
		Require(parser.getValue("h", "") == "40", "/h=40 must still set height");
		Require(parser.getValue("threads", "") == "8", "/threads=8 must still parse");
		Require(parser.getUnrecognized().empty(),
			"a known option must not be reported as unrecognized");
	}

	// A '/'-prefixed token that is not an option is a file, not a typo to
	// report - but a '-'-prefixed one is still a mistyped option.
	{
		CommandLineParser parser = MakeParser();
		parser.parseTokens({"-nonsense"});
		Require(!parser.getUnrecognized().empty(),
			"a mistyped -option must still be reported");
	}

	std::cout << "CommandLineParserTests passed\n";
	return 0;
}
