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
#include <Multiplexer.hpp>


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
		std::cerr << e.what() << std::endl;
	} catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
	try {
		Multiplexer multiplexer(v_configs);
		for (size_t i = 0; i < multiplexer.v_servers.size(); i++)
		{
			std::cout << "listen " << multiplexer.v_servers[i]->m_addr.sin_addr.s_addr << ":" << multiplexer.v_servers[i]->m_addr.sin_port << std::endl;
		}
        multiplexer.startup();
	}
	catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
}
