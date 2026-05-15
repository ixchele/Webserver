#pragma once

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <fstream>
#include <vector>
#include <deque>
#include <iostream>
#include <Token.hpp>

// struct Token {
//     std::string		content;
//     int				line;
// 	int				column;
//
// 	Token();
// 	Token(const std::string &content, int line, int column)
// 		: content(content), line(line), column(column) {}
// };


std::ostream& operator<<(std::ostream& os, const Token& t);

std::deque<Token>	tokenizer(const std::string &confContent);
void				configParser(const std::string &confPath);

#include <ConfigFileParser.template.hpp>
