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
#include <netinet/in.h>

int main(int ac, char **av)
{
	if (ac != 2)
		return 1;
	TokenList tokenList = tokenizer(av[1]);
	ConfigParser lexer(tokenList);
	std::vector<ServerConfig *> v_configs;
	try
	{
		v_configs = lexer.parse();
		// std::cout << *v_configs[0];
	}
	catch (ConfigParser::ConfigException &e)
	{
		std::cerr << e.what() << std::endl;
	}
	catch (std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}
	try
	{
		Multiplexer multiplexer(v_configs);
		std::map<std::string, Server *>::iterator it;
		for (it = multiplexer.m_servers.begin(); it != multiplexer.m_servers.end(); ++it)
		{
			std::cout << "listen " << it->second->m_ip << ":" << it->second->m_port << std::endl;
		}
		multiplexer.startup();
	}
	catch (std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}
}
