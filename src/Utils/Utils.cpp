#include <Utils.hpp>
#include <vector>

std::string formatError(const char* format, ...) {
	char buffer[1024];
	va_list args;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	return std::string(buffer);
}

std::vector<std::string> ft_split(std::string &str, char delimiter) {
	std::vector<std::string> tokens;
}
