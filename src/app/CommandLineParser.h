/*
CommandLineParser.h

Lightweight in-house command line parser used by RastaConverter.
Goals:
- Accept option prefixes: '/', '-', '--' (keeps backward compatibility)
- Support switches (flags) and key/value options (name=value or name value)
- Case-insensitive option names
- Aliases (e.g. 's' and 'solutions')
- Generate help text from a single specification to avoid duplication

Not over-engineered: a tiny dependency-free parser that fits our needs.
*/
#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>

class CommandLineParser
{
public:
	// Option specification used for parsing and help generation
	struct OptionSpec {
		std::string canonicalName; // lower-case canonical key (e.g. "input")
		std::vector<std::string> aliases; // also lower-case
		bool takesValue = false; // true if option expects a value
		bool optionalValue = false; // if present without explicit value, use implicitValue
		std::string valueName; // e.g. "FILE", "N"
		std::string defaultValue; // only for value options
		std::string implicitValue; // used when optionalValue == true and value omitted
		std::string help; // human description
		std::string group; // section in help (e.g. "General", "Image processing")
	};

	CommandLineParser();

	// Specification API
	void addFlag(const std::string &canonicalName,
			  const std::vector<std::string> &aliases,
			  const std::string &help,
			  const std::string &group);

	void addOption(const std::string &canonicalName,
			   const std::vector<std::string> &aliases,
			   const std::string &valueName,
			   const std::string &defaultValue,
			   const std::string &help,
			   const std::string &group,
			   bool optionalValue = false,
			   const std::string &implicitValue = "");

	// Parsing API
	void parse(int argc, char *argv[]);

	// Query API (case-insensitive names)
	bool switchExists(const std::string &name) const; // true if flag present or value provided
	std::string getValue(const std::string &name, const std::string &defaultValue) const;
	bool nonInterpretedExists(const std::string &value) const; // positional match (case-insensitive)

	// New helpers for advanced CLI workflows
	const std::vector<std::string>& getPositionalArguments() const { return positional; }
	bool valueProvided(const std::string &name) const;
	bool flagProvided(const std::string &name) const;
	void parseTokens(const std::vector<std::string>& tokens);
	void mergeFrom(const CommandLineParser& overrides, bool replacePositionals);
	std::string rebuildCommandLine() const;
	std::vector<std::string> getNormalizedTokens() const;
	bool verifyCompulsory(const std::vector<std::string> &pairs = {},
					  const std::vector<std::string> &switches = {},
					  const std::vector<std::string> &nonInterpreted = {});

	// Help/usage
	std::string formatHelp(const std::string &appName) const;

	// Diagnostics
	const std::vector<std::string>& getUnrecognized() const { return unrecognized; }
	const std::vector<std::string>& getMissingValueOptions() const { return missingValueOptions; }
	std::vector<std::string> allOptionNames() const;

	// Public counters and reconstructed command line (for compatibility)
	int mn_NonInterpreted = 0;
	int mn_Switches = 0;
	int mn_PairCount = 0;
	std::string mn_command_line;

private:
	// Internal helpers
	static std::string toLower(const std::string &s);
	const OptionSpec* findSpec(const std::string &lowerKey) const; // find by alias or canonical
	std::string canonicalOf(const std::string &lowerKey) const; // returns canonical or ""
	bool isPrefixed(const std::string &token) const; // starts with '/', '-', or '--'

	// Specification data
	std::vector<OptionSpec> optionSpecs;
	std::unordered_map<std::string, size_t> aliasToIndex; // alias->spec index

	// Parsed data
	std::unordered_map<std::string, std::string> values; // canonicalName -> value
	std::unordered_set<std::string> flags; // canonicalName present
	std::vector<std::string> positional; // non-prefixed tokens
	std::vector<std::string> unrecognized; // original tokens that didn't match any spec
	std::vector<std::string> missingValueOptions; // canonical names missing required values
	std::vector<std::string> buildNormalizedTokens() const;

	static std::string joinTokens(const std::vector<std::string>& tokens);
};


