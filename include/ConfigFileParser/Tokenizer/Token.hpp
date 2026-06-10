#pragma once

#include <deque>
#include <iostream>
#include <sstream>

struct Token {
	std::string		content;
	int				line;
	int				column;

	Token();
	Token(const std::string &content, int line, int column)
		: content(content), line(line), column(column) {}

	std::string	str(void) {
		std::stringstream	ssStr;
		ssStr << content << " " << line << " " << column;
		return ssStr.str();
	};
};

typedef std::deque<Token> TokenList;

std::ostream& operator<<(std::ostream& os, const Token& t);
std::deque<Token>	tokenizer(const std::string &confContent);
