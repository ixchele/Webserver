#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

class	ConfigFile {
	private:
		std::string	_confContent;

	public:
		ConfigFile(const std::string &confPath);

		struct ConfigException : public std::runtime_error {
			ConfigException(const std::string &error);
		};
};

ConfigFile::ConfigFile(const std::string &confPath) {
	std::ifstream	ifs(confPath.c_str());	
	if (!ifs.is_open())
		throw ConfigException("[x] Error : could not open config file \'" + confPath + "'" + ".");
	
	ifs >> this->_confContent;
}

std::string	AereContent(const std::string	&content, const std::string &tokens) {
	std::string	spaced;

	for (std::size_t i = 0; i < content.size(); ++i) {
		if (tokens.find(content[i]) != std::string::npos)
			spaced += " " + std::string(&content[i]) + " ";
		else
			spaced += content[i];
	}

	return spaced;
}

std::vector<std::string>	toknizer(const std::string &content) {
	std::stringstream	ss(AereContent(content, "{};"));
	std::string			token;
	std::vector<std::string>	tokenList;

	while (ss >> token)
		tokenList.push_back(token);

	return tokenList;
}
