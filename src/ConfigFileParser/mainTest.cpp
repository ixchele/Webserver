// #include <ConfigFile.hpp>
// #include <ConfigFileParser.template.hpp>
// #include <vector>
#include "ServerConfig.hpp"
#include <ConfigParser.hpp>
#include <exception>
#include <iostream>
#include <ostream>
#include <vector>


int	main(int ac, char **av) {
	if (ac != 2)
		return 1;
	TokenList	tokenList = tokenizer(av[1]);
	ConfigParser	lexer(tokenList);
	std::vector<ServerConfig*> servers;
	try {
		servers = lexer.parse();
		std::cout << *servers[0];
	} catch (ConfigParser::ConfigException &e) {
		std::cout << e.what() << std::endl;
	} catch (std::exception &e) {
		std::cout << e.what() << std::endl;
	}
}
