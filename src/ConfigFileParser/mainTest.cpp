// #include <ConfigFile.hpp>
// #include <ConfigFileParser.template.hpp>
// #include <vector>
#include <ConfigParser.hpp>
#include <iostream>
#include <ostream>
#include <vector>


int	main(int ac, char **av) {
	if (ac != 2)
		return 1;
	try {
		TokenList	tokenList = tokenizer(av[1]);
		ConfigParser	lexer = ConfigParser(tokenList);
		std::vector<ServerConfig> servers = lexer.parse();
		std::cout << servers[0];

	} catch (ConfigParser::ConfigException &e) {
		std::cout << e.what() << std::endl;
	}
}
