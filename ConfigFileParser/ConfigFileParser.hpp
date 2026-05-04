#pragma once

#include <string>
#include <vector>
#include <sstream>

typedef std::string	Token;
typedef std::vector<Token> TokenList;

TokenList	toknizer(const std::string &content);


#include <ConfigFileParser.template.hpp>
