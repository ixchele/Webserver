// #include <ConfigFile.hpp>
// #include <ConfigFileParser.template.hpp>
// #include <vector>
#include "ConfigFileParser.template.hpp"
#include <ConfigParser.hpp>
#include <iostream>

int	main(int ac, char **av) {
	if (ac != 2)
		return 1;
	TokenList	tokenList = tokenizer(av[1]);
	ConfigParser	lexer = ConfigParser(tokenList);
	lexer.parse();
}
