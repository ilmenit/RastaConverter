/*
CommandLineParser.cpp

Small, dependency-free command line parser with aliasing and unified prefixes.
It supports:
 - Options beginning with '/', '-', or '--'
 - name=value and name value forms
 - case-insensitive option names
 - flags (no value) and value options
 - generated help from a single specification
*/

#include "CommandLineParser.h"

#include <algorithm>
#include <sstream>
#include <map>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

static bool iequals_char(char a, char b) {
	return (a == b) || (('A' <= a && a <= 'Z') && (a - 'A' + 'a') == b);
}

static bool istarts_with(const std::string &s, const char *prefix) {
	size_t n = 0; while (prefix[n] != '\0') ++n;
	if (s.size() < n) return false;
	for (size_t i = 0; i < n; ++i) {
		if (!iequals_char(s[i], (char)tolower(prefix[i]))) return false;
	}
	return true;
}

static bool isSignedNumberToken(const std::string &s) {
	if (s.empty()) return false;
	size_t i = 0;
	if (s[i] == '-' || s[i] == '+') ++i;
	bool anyDigit = false;
	for (; i < s.size(); ++i) {
		unsigned char c = (unsigned char)s[i];
		if (std::isdigit(c)) { anyDigit = true; continue; }
		if (c == '.') continue;
		return false;
	}
	return anyDigit;
}

static std::string stripQuotes(const std::string &s) {
	if (s.size() >= 2) {
		char b = s.front();
		char e = s.back();
		if ((b == '"' && e == '"') || (b == '\'' && e == '\'')) {
			return s.substr(1, s.size() - 2);
		}
	}
	return s;
}

static std::string quoteIfNeeded(const std::string& token) {
	bool needsQuote = token.empty();
	for (char c : token) {
		if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\\') {
			needsQuote = true;
			break;
		}
	}
	if (!needsQuote) {
		return token;
	}
	std::string out;
	out.reserve(token.size() + 2);
	out.push_back('"');
	for (char c : token) {
		if (c == '"' || c == '\\') {
			out.push_back('\\');
		}
		out.push_back(c);
	}
	out.push_back('"');
	return out;
}

std::string CommandLineParser::toLower(const std::string &s) {
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return (char)tolower(c); });
	return r;
}

CommandLineParser::CommandLineParser() {}

std::string CommandLineParser::joinTokens(const std::vector<std::string>& tokens) {
	std::string out;
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (i) out += " ";
		out += quoteIfNeeded(tokens[i]);
	}
	return out;
}

std::vector<std::string> CommandLineParser::buildNormalizedTokens() const {
	std::vector<std::string> tokens;
	tokens.reserve(positional.size() + values.size() + flags.size() + unrecognized.size());
	tokens.insert(tokens.end(), positional.begin(), positional.end());
	std::unordered_set<std::string> emitted;
	emitted.reserve(optionSpecs.size());

	for (const auto &spec : optionSpecs) {
		const std::string &canon = spec.canonicalName;
		if (spec.takesValue) {
			auto it = values.find(canon);
			if (it == values.end()) continue;
			std::string token = "/" + canon;
			if (!it->second.empty()) token += "=" + it->second;
			tokens.push_back(token);
			emitted.insert(canon);
		} else {
			if (flags.find(canon) == flags.end()) continue;
			tokens.push_back("/" + canon);
			emitted.insert(canon);
		}
	}

	// Include any remaining values/flags (typically unrecognized aliases)
	for (const auto &kv : values) {
		if (emitted.find(kv.first) != emitted.end()) continue;
		std::string token = "/" + kv.first;
		if (!kv.second.empty()) token += "=" + kv.second;
		tokens.push_back(token);
		emitted.insert(kv.first);
	}
	for (const auto &flag : flags) {
		if (emitted.find(flag) != emitted.end()) continue;
		tokens.push_back("/" + flag);
		emitted.insert(flag);
	}

	// Preserve original unrecognized tokens verbatim
	for (const auto &tok : unrecognized) tokens.push_back(tok);

	return tokens;
}

std::vector<std::string> CommandLineParser::getNormalizedTokens() const
{
	return buildNormalizedTokens();
}

void CommandLineParser::addFlag(const std::string &canonicalName,
			  const std::vector<std::string> &aliases,
			  const std::string &help,
			  const std::string &group)
{
	OptionSpec spec;
	spec.canonicalName = toLower(canonicalName);
	spec.aliases.reserve(1 + aliases.size());
	spec.aliases.push_back(spec.canonicalName);
	for (const auto &a : aliases) spec.aliases.push_back(toLower(a));
	spec.takesValue = false;
	spec.help = help;
	spec.group = group;
	// Collision detection: any alias or canonical already registered?
	for (const auto &k : spec.aliases) {
		auto it = aliasToIndex.find(k);
		if (it != aliasToIndex.end()) {
			const auto &other = optionSpecs[it->second];
			throw std::runtime_error("CommandLineParser: option alias '" + k + "' conflicts with existing option '" + other.canonicalName + "'");
		}
	}
	optionSpecs.push_back(spec);
	size_t idx = optionSpecs.size() - 1;
	for (const auto &k : optionSpecs.back().aliases) aliasToIndex[k] = idx;
}

void CommandLineParser::addOption(const std::string &canonicalName,
			   const std::vector<std::string> &aliases,
			   const std::string &valueName,
			   const std::string &defaultValue,
			   const std::string &help,
			   const std::string &group,
			   bool optionalValue,
			   const std::string &implicitValue)
{
	OptionSpec spec;
	spec.canonicalName = toLower(canonicalName);
	spec.aliases.reserve(1 + aliases.size());
	spec.aliases.push_back(spec.canonicalName);
	for (const auto &a : aliases) spec.aliases.push_back(toLower(a));
	spec.takesValue = true;
	spec.optionalValue = optionalValue;
	spec.valueName = valueName;
	spec.defaultValue = defaultValue;
	spec.implicitValue = implicitValue;
	spec.help = help;
	spec.group = group;
	// Collision detection: any alias or canonical already registered?
	for (const auto &k : spec.aliases) {
		auto it = aliasToIndex.find(k);
		if (it != aliasToIndex.end()) {
			const auto &other = optionSpecs[it->second];
			throw std::runtime_error("CommandLineParser: option alias '" + k + "' conflicts with existing option '" + other.canonicalName + "'");
		}
	}
	optionSpecs.push_back(spec);
	size_t idx = optionSpecs.size() - 1;
	for (const auto &k : optionSpecs.back().aliases) aliasToIndex[k] = idx;
}

bool CommandLineParser::isPrefixed(const std::string &token) const {
	if (token.empty()) return false;
	char c0 = token[0];
	if (c0 == '/' || c0 == '-') return true;
	return false;
}

const CommandLineParser::OptionSpec* CommandLineParser::findSpec(const std::string &lowerKey) const {
	auto it = aliasToIndex.find(lowerKey);
	if (it == aliasToIndex.end()) return nullptr;
	return &optionSpecs[it->second];
}

std::string CommandLineParser::canonicalOf(const std::string &lowerKey) const {
	auto it = aliasToIndex.find(lowerKey);
	if (it == aliasToIndex.end()) return std::string();
	return optionSpecs[it->second].canonicalName;
}

void CommandLineParser::parse(int argc, char *argv[])
{
	std::vector<std::string> tokens;
	tokens.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
	for (int i = 1; i < argc; ++i) tokens.emplace_back(argv[i]);
	parseTokens(tokens);
}

void CommandLineParser::parseTokens(const std::vector<std::string>& tokens)
{
	mn_NonInterpreted = 0;
	mn_Switches = 0;
	mn_PairCount = 0;
	mn_command_line.clear();
	values.clear();
	flags.clear();
	positional.clear();
	unrecognized.clear();
	missingValueOptions.clear();

	for (size_t idx = 0; idx < tokens.size(); ++idx) {
		const std::string &token = tokens[idx];

		if (!isPrefixed(token)) {
			++mn_NonInterpreted;
			positional.push_back(token);
			continue;
		}

		std::string keyval = token;
		if (istarts_with(keyval, "--")) keyval.erase(0, 2);
		else keyval.erase(0, 1);

		std::string key;
		std::string val;
		size_t eq = keyval.find('=');
		if (eq != std::string::npos) {
			key = keyval.substr(0, eq);
			val = keyval.substr(eq + 1);
		} else {
			key = keyval;
		}

		key = toLower(key);
		const OptionSpec *spec = findSpec(key);
		if (!spec) {
			if (!val.empty()) {
				++mn_PairCount;
				values[key] = stripQuotes(val);
			} else {
				++mn_Switches;
				flags.insert(key);
			}
			if (std::find(unrecognized.begin(), unrecognized.end(), token) == unrecognized.end())
				unrecognized.push_back(token);
			continue;
		}

		if (!spec->takesValue) {
			++mn_Switches;
			flags.insert(spec->canonicalName);
			continue;
		}

		if (val.empty() && idx + 1 < tokens.size()) {
			std::string next = tokens[idx + 1];
			if (!isPrefixed(next) || isSignedNumberToken(next)) {
				val = next;
				++idx;
			}
		}
		if (val.empty() && spec->optionalValue) {
			val = spec->implicitValue;
		}
		if (val.empty()) {
			if (spec->takesValue && !spec->optionalValue) {
				missingValueOptions.push_back(spec->canonicalName);
			}
		}
		if (!val.empty()) ++mn_PairCount; else ++mn_Switches;
		values[spec->canonicalName] = stripQuotes(val);
	}

	mn_command_line = joinTokens(tokens);
}

bool CommandLineParser::switchExists(const std::string &name) const {
	std::string k = toLower(name);
	std::string canon = canonicalOf(k);
	if (canon.empty()) canon = k;
	if (flags.find(canon) != flags.end()) return true;
	if (values.find(canon) != values.end()) return true;
	return false; 
}

std::string CommandLineParser::getValue(const std::string &name, const std::string &defaultValue) const {
	std::string k = toLower(name);
	std::string canon = canonicalOf(k);
	if (canon.empty()) canon = k;
	auto it = values.find(canon);
	if (it == values.end() || it->second.empty()) return defaultValue;
	return it->second;
}

bool CommandLineParser::valueProvided(const std::string &name) const {
	std::string canon = canonicalOf(toLower(name));
	if (canon.empty()) canon = toLower(name);
	return values.find(canon) != values.end();
}

bool CommandLineParser::flagProvided(const std::string &name) const {
	std::string canon = canonicalOf(toLower(name));
	if (canon.empty()) canon = toLower(name);
	if (flags.find(canon) != flags.end()) return true;
	// treat value presence for optional flags (e.g., implicit on) as provided
	return values.find(canon) != values.end();
}

void CommandLineParser::mergeFrom(const CommandLineParser& overrides, bool replacePositionals)
{
	if (replacePositionals) {
		positional = overrides.positional;
		mn_NonInterpreted = static_cast<int>(positional.size());
	} else if (!overrides.positional.empty()) {
		positional = overrides.positional;
		mn_NonInterpreted = static_cast<int>(positional.size());
	}

	for (const auto &kv : overrides.values) {
		values[kv.first] = kv.second;
	}
	for (const auto &flag : overrides.flags) {
		flags.insert(flag);
	}
	for (const auto &tok : overrides.unrecognized) {
		if (std::find(unrecognized.begin(), unrecognized.end(), tok) == unrecognized.end())
			unrecognized.push_back(tok);
	}

	// Re-parse normalized tokens to refresh counters/state
	parseTokens(buildNormalizedTokens());
}

std::string CommandLineParser::rebuildCommandLine() const
{
	return joinTokens(buildNormalizedTokens());
}

bool CommandLineParser::nonInterpretedExists(const std::string &value) const {
	std::string needle = toLower(value);
	for (const auto &s : positional) {
		if (toLower(s) == needle) return true;
	}
	return false;
}

bool CommandLineParser::verifyCompulsory(const std::vector<std::string> &pairs,
					  const std::vector<std::string> &switches,
					  const std::vector<std::string> &nonInterpreted)
{
	for (const auto &p : pairs) {
		if (getValue(p, "").empty()) return false;
	}
	for (const auto &s : switches) {
		if (!switchExists(s)) return false;
	}
	for (const auto &n : nonInterpreted) {
		if (!nonInterpretedExists(n)) return false;
	}
			return true;
		}

std::string CommandLineParser::formatHelp(const std::string &appName) const
{
	// Group options
	std::map<std::string, std::vector<const OptionSpec*>> grouped;
	for (const auto &spec : optionSpecs) {
		grouped[spec.group].push_back(&spec);
	}

	std::ostringstream out;
	out << "Usage:\n  " << appName << " <InputFile> [options]\n\n";
	out << "Notes:\n";
	out << "  - Options accept '/' or '-'/'--' prefixes. Examples: /threads=4, -threads 4, --threads 4\n";
	out << "  - Input file can be positional or provided via -i/--input\n";
	out << "  - Use --quiet to run without GUI and print errors to stderr\n\n";
    // General rule for on/off options
    out << "  - On/off options accept a bare switch as 'on'; absence equals 'off' (e.g., /dual)\n\n";

	// Preferred group order by importance
	std::vector<std::string> preferredGroups = {
		"General options",
		"Input",
		"Image processing",
		"Dual-frame mode"
	};

	std::vector<std::string> orderedGroups;
	for (const auto &g : preferredGroups) if (grouped.count(g)) orderedGroups.push_back(g);
	for (const auto &kv : grouped) {
		if (std::find(orderedGroups.begin(), orderedGroups.end(), kv.first) == orderedGroups.end()) {
			orderedGroups.push_back(kv.first);
		}
	}

	for (const auto &groupName : orderedGroups) {
		out << groupName << ":\n";
		for (const OptionSpec* s : grouped[groupName]) {
			// Primary canonical name
			std::string primary = (s->canonicalName.size() == 1)
							  ? ("-" + s->canonicalName)
							  : ("--" + s->canonicalName);
			if (s->takesValue) {
				primary += " <" + (s->valueName.empty() ? std::string("VALUE") : s->valueName) + ">";
			}
			out << "  " << primary;
			if (!s->defaultValue.empty()) out << " (default: " << s->defaultValue << ")";
			out << "\n    " << s->help << "\n";

			// Aliases line: show alias words only (no prefix variants), excluding canonical
			std::vector<std::string> aliasWords;
			aliasWords.reserve(s->aliases.size());
			for (const auto &a : s->aliases) {
				if (a == s->canonicalName) continue;
				if (std::find(aliasWords.begin(), aliasWords.end(), a) == aliasWords.end()) aliasWords.push_back(a);
			}
			if (!aliasWords.empty()) {
				out << "      Aliases: ";
				for (size_t i = 0; i < aliasWords.size(); ++i) {
					if (i) out << ", ";
					out << aliasWords[i];
				}
				out << "\n";
			}
		}
		out << "\n";
	}
	return out.str();
}

std::vector<std::string> CommandLineParser::allOptionNames() const
{
    std::vector<std::string> out;
    out.reserve(optionSpecs.size());
    for (const auto &s : optionSpecs) out.push_back(s.canonicalName);
    return out;
}


