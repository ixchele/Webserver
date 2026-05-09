#include <ConfigFileParser.hpp>

TokenList	toknizer(const std::string &content) {
	std::stringstream			ss(content);
	std::string					token;
	std::vector<std::string>	tokenList;

	while (ss >> token)
		tokenList.push_back(token);

	return tokenList;
}
