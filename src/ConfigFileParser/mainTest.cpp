// #include <ConfigFile.hpp>
// #include <ConfigFileParser.template.hpp>
// #include <vector>
#include "ServerConfig.hpp"
#include <ConfigParser.hpp>
#include <exception>
#include <iostream>
#include <ostream>
#include <vector>
#include <Server.hpp>


int	main(int ac, char **av) {
	if (ac != 2)
		return 1;
	TokenList	tokenList = tokenizer(av[1]);
	ConfigParser	lexer(tokenList);
	std::vector<ServerConfig*> v_configs;
	try {
		v_configs = lexer.parse();
		std::cout << *v_configs[0];
	} catch (ConfigParser::ConfigException &e) {
		std::cout << e.what() << std::endl;
	} catch (std::exception &e) {
		std::cout << e.what() << std::endl;
	}
	std::vector<Server> v_servers;
	Server::generate_servers(v_servers, v_configs);
	for (size_t i = 0; i < v_servers.size(); i++)
	{
		std::cout << "Port [" << i << "]: " << v_servers[i].get_fd() << std::endl;
	}
}
