#pragma once

#include <initializer_list>
#include <string>

// Saved-run recipes are command lines, but callers work with decoded values.
// These helpers own option replacement and path relocation so no caller has to
// hand-quote a Windows path or edit an escaped representation as plain text.
std::string SetRecipeOption(const std::string& recipe,
	const std::string& name, const std::string& value,
	std::initializer_list<std::string> aliases = {});

std::string RelocateRecipePathPrefix(const std::string& recipe,
	const std::string& from, const std::string& to, bool* relocated = nullptr);
