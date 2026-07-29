#include "XexBuild.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "Desktop.h"

namespace rc_live_ui {
namespace {

std::string DirectoryOf(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string WithoutExtension(const std::string& name)
{
	const size_t dot = name.find_last_of('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

bool Exists(const std::string& path)
{
	return !path.empty() && SDL_GetPathInfo(path.c_str(), nullptr);
}

std::int64_t ModifiedTime(const std::string& path)
{
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path.c_str(), &info))
		return 0;
	return info.modify_time;
}

// A dual-frame run writes its artifacts under fixed out_dual_A/B names, which
// is exactly what GeneratorDual's source expects; a single-frame run names them
// after its output picture. Asking the folder is more reliable than re-parsing
// the recorded command line, which old runs may not have.
bool IsDualRun(const std::string& folder)
{
	return Exists(folder + "/out_dual_A.opt");
}

// The bundled assembler for this host. The Generator folder also ships a `mads`
// shell launcher that picks the same file, but a launcher only works if its
// executable bit survived the trip - a zip download, a copy from Windows - so
// the binary is named outright and the launcher is only the fallback.
std::string AssemblerPath(const std::string& generator_dir)
{
#if defined(_WIN32)
	const char* preferred = "/mads.exe";
#elif defined(__APPLE__)
	const char* preferred = "/mads-macos-arm64";
#else
	const char* preferred = "/mads-linux-x86_64";
#endif
	const std::string candidate = generator_dir + preferred;
	if (Exists(candidate))
		return candidate;
	const std::string launcher = generator_dir + "/mads";
	return Exists(launcher) ? launcher : std::string();
}

std::string Quote(const std::string& text)
{
#if defined(_WIN32)
	return "\"" + text + "\"";
#else
	std::string quoted = "'";
	for (char c : text) {
		if (c == '\'')
			quoted += "'\\''";
		else
			quoted += c;
	}
	quoted += "'";
	return quoted;
#endif
}

std::string RunCommand(const std::string& command, int* status)
{
#if defined(_WIN32)
	FILE* pipe = _popen(command.c_str(), "r");
#else
	FILE* pipe = popen(command.c_str(), "r");
#endif
	if (pipe == nullptr) {
		*status = -1;
		return std::string();
	}
	std::string output;
	char buffer[512];
	while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
		output += buffer;
#if defined(_WIN32)
	*status = _pclose(pipe);
#else
	*status = pclose(pipe);
#endif
	return output;
}

} // namespace

std::string RunXexPath(const std::string& output_base)
{
	if (output_base.empty())
		return std::string();
	const std::string folder = DirectoryOf(output_base);
	if (IsDualRun(folder))
		return folder + "/out_dual.xex";
	return folder + "/" + WithoutExtension(FileName(output_base)) + ".xex";
}

bool RunXexIsCurrent(const std::string& output_base)
{
	const std::string xex = RunXexPath(output_base);
	if (!Exists(xex))
		return false;
	const std::string folder = DirectoryOf(output_base);
	// The optimized raster program is the last thing a save writes that the
	// generator reads, so it is the honest "is this stale" comparison.
	const std::string program = IsDualRun(folder)
		? folder + "/out_dual_A.opt" : output_base + ".opt";
	if (!Exists(program))
		return true;
	return ModifiedTime(xex) >= ModifiedTime(program);
}

XexBuildResult BuildRunXex(const std::string& output_base)
{
	XexBuildResult result;
	if (output_base.empty()) {
		result.log = "This run did not record where it wrote its files.";
		return result;
	}

	const std::string folder = DirectoryOf(output_base);
	const bool dual = IsDualRun(folder);
	const std::string generator = BundledPath(dual ? "GeneratorDual" : "Generator");
	const bool antic4 = !dual && Exists(output_base + ".a4.scr")
		&& Exists(output_base + ".a4.fnt");
	const std::string source = generator
		+ (antic4 ? "/antic4.asq" : "/no_name.asq");
	if (!Exists(source)) {
		result.log = "The bundled generator is missing: no " + source
			+ ". It ships in the " + (dual ? std::string("GeneratorDual")
				: std::string("Generator")) + " folder beside the program.";
		return result;
	}
	const std::string assembler = AssemblerPath(generator);
	if (assembler.empty()) {
		result.log = "No MADS assembler for this platform in " + generator + ".";
		return result;
	}

	// The single-frame generator source names its inputs "output.png.*", which
	// is what they were called back when every conversion wrote into one shared
	// folder. Rather than copying a run's artifacts under those names, the
	// source is retargeted at the run's own names and assembled where it stands.
	std::ifstream in(source, std::ios::binary);
	if (!in) {
		result.log = "Could not read " + source + ".";
		return result;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::string text = buffer.str();
	in.close();

	if (!dual) {
		const std::string base = FileName(output_base);
		const std::string placeholder = "output.png";
		for (size_t at = text.find(placeholder); at != std::string::npos;
			at = text.find(placeholder, at + base.size()))
			text.replace(at, placeholder.size(), base);
	}

	const std::string script = folder + "/.rasta-xex.asq";
	{
		std::ofstream out(script, std::ios::binary);
		if (!out) {
			result.log = "Could not write " + script
				+ ". Is the run folder read-only?";
			return result;
		}
		out << text;
	}

	const std::string xex = RunXexPath(output_base);
	SDL_RemovePath(xex.c_str());

	// Both include paths: the generator's own headers and macros, and the run
	// folder holding the data the source pulls in.
	std::string command = Quote(assembler) + " " + Quote(script)
		+ " " + Quote("-o:" + xex)
		+ " " + Quote("-i:" + generator)
		+ " " + Quote("-i:" + folder)
		+ " 2>&1";
	int status = 0;
	result.log = RunCommand(command, &status);
	SDL_RemovePath(script.c_str());

	if (Exists(xex)) {
		result.ok = true;
		result.xex_path = xex;
		return result;
	}
	if (status == -1 && result.log.empty())
		result.log = "Could not start " + assembler + ".";
	else if (result.log.empty())
		result.log = "The assembler produced no executable and said nothing.";
	return result;
}

} // namespace rc_live_ui
