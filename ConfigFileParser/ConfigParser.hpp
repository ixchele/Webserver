#pragma once

#include <Token.hpp>
#include <deque>
#include <map>
#include <vector>

#define GET 1
#define POST 2
#define DELETE 4
class	ConfigParser {
	private:
		typedef void (ConfigParser::*MemFunc)();


		std::map<std::string, MemFunc> serverDirectives;
		std::map<std::string, MemFunc> locationDirectives;
		std::map<std::string, MemFunc> communDirectives;
		std::map<std::string, int>	methodsAvailable;

		TokenList			tokenList;
		TokenList::const_iterator	it;

		// NOTE : Helpers
		bool	peek(const std::string &expected);
		void	consume(const std::string &expected);

		// NOTE : Parsing functions

		// Dispatchers
		void        config();
		void        serverDirective();
		void        serverBlock();
		void        locationDirective();
		void        locationBlock();
		// Server directives
		void        listenDir();
		void        hostDir();
		void        nameDir();
		void        errorPageDir();
		void        clientBodyDir();
		// Location directives
		void        methodsDir();
		void        rootDir();
		void        indexDir();
		void        autoindexDir();
		void        returnDir();
		void        uploadDir();
		void        cgiPassDir();


	public:
		ConfigParser(const TokenList &tokenList);

		std::vector<std::string>	parse();
};
