// #include <ConfigFile.hpp>
// #include <ConfigFileParser.template.hpp>
// #include <vector>
#include "ConfigFile.hpp"
#include "ConfigFileParser.template.hpp"
#include <ConfigParser.hpp>
#include <iostream>

int	main(void) {
	TokenList	tokenList = tokenizer("configTest.conf");
	ConfigParser	lexer = ConfigParser(tokenList);
	lexer.parse();
}
