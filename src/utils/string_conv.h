#include <sstream>
#include <iomanip>
#include <string>

template<class T>
std::string Value2String(T value)
{
	std::string result;
	std::stringstream ss;
	ss << value;
	ss >> result;
	return result;
}

template<class T>
T String2Value(const std::string &s)
{
	T result;
	std::stringstream ss(s);
	ss >> result;
	return result;
}

template<class T>
T String2HexValue(const std::string &s)
{
	T result;
	std::stringstream ss;
	ss << std::hex << s;
	ss >> (std::hex) >> result;
	return result;
}
