#include <sstream>
#include <iomanip>
#include <string>

template<class T>
std::string Value2String(T value)
{
	string result;
	std::stringstream ss;
	ss << std::setw(2) << std::setfill('0') << value;
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
	ss >> (hex) >> result;
	return result;
}
