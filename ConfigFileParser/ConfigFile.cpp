#include "ConfigFileParser.hpp"
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <fstream>
#include <vector>

enum	TokenType {
	WORD,
	BRACKET_O,
	BRACKET_C,
	SEMICOLON,
};

struct Token {
    std::string		content;
    TokenType		type;
    int				line;
	int				column;

	Token();
	Token(const std::string &content, TokenType type, int line, int column)
		: content(content), type(type), line(line), column(column) {}
	Token(const std::string &content, int line, int column)
		: content(content), line(line), column(column) {}
};

class	ConfigFile {
	private:

		std::string	_confContent;
		int			_line;
		int			_col;

		std::vector<Token>	_tokenizer(void);	

	public:

		typedef  struct Token Token;

		struct ConfigException : public std::runtime_error {
			ConfigException(const std::string &error);
		};

		ConfigFile(const std::string &confPath);

};

ConfigFile::ConfigFile(const std::string &confPath) : _line(1), _col(1) {
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

std::vector<Token>	ConfigFile::_tokenizer(void) {
	std::string			delim("{};");
	std::vector<Token>	tokenList;
	std::string			word;
	int startCol = 1;

	for (std::size_t i = 0; i < this->_confContent.size(); ++i) {
		char	c = this->_confContent[i];

		if (c == '\n') {
			if (!word.empty()) 
				tokenList.push_back(Token(word, this->_line, startCol));
			word.clear();
			this->_line++;
			this->_col = 1;
			continue;
		}

		if (delim.find(c) == std::string::npos) {
			if (!word.empty())
				tokenList.push_back(Token(word, this->_line, startCol));

			tokenList.push_back(Token(std::string(1, c), this->_line, this->_col));
			word.clear();
		}
		else if (isspace(c)) {
			if (!word.empty())
				tokenList.push_back(Token(word, this->_line, startCol));
			word.clear();
		}
		else {
			if (word.empty())
				startCol = this->_col;
			word += c;
		}
		this->_col++;
	}

	return tokenList;
}


