#include <sstream>
#include <iomanip>
#include <string>
#include <type_traits>
#include <algorithm>

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

// Insert thousands separators (',') into the integer part of a numeric string.
// Works for negative numbers and preserves any fractional part after '.'
inline std::string insert_thousands_commas(const std::string &numeric)
{
	if (numeric.empty()) return numeric;

	// Handle sign
	size_t start = 0;
	bool negative = false;
	if (numeric[0] == '-') { negative = true; start = 1; }

	// Split into integer and fractional parts
	size_t dot_pos = numeric.find('.');
	const size_t int_end = (dot_pos == std::string::npos) ? numeric.size() : dot_pos;
	std::string int_part = numeric.substr(start, int_end - start);
	std::string frac_part = (dot_pos == std::string::npos) ? std::string() : numeric.substr(dot_pos);

	// Insert commas into integer part using forward grouping for simplicity
	std::string out;
	const size_t n = int_part.size();
	if (n == 0) {
		out = "";
	} else if (n <= 3) {
		out = int_part;
	} else {
		const size_t groups = (n - 1) / 3; // number of commas to insert
		const size_t first_group_len = (n % 3 == 0) ? 3 : (n % 3);
		out.reserve(n + groups);
		// First group (could be 1-3 digits)
		out.append(int_part.data(), first_group_len);
		// Remaining groups of 3 digits prefixed by commas
		for (size_t i = first_group_len; i < n; i += 3) {
			out.push_back(',');
			out.append(int_part.data() + i, 3);
		}
	}

	if (negative) out.insert(out.begin(), '-');
	out += frac_part; // append fractional part unchanged
	return out;
}

// Generic integral formatter with thousands separators
template <typename T>
inline typename std::enable_if<std::is_integral<T>::value, std::string>::type
format_with_commas(T value)
{
	return insert_thousands_commas(std::to_string(value));
}

// Floating-point formatter: preserve default to_string fractional digits; add separators to integer part
inline std::string format_with_commas(double value)
{
	return insert_thousands_commas(std::to_string(value));
}

inline std::string format_with_commas(float value)
{
	return insert_thousands_commas(std::to_string(value));
}

inline std::string format_with_commas(long double value)
{
	// std::to_string(long double) may have implementation-defined precision; keep consistent with others
	return insert_thousands_commas(std::to_string((double)value));
}
