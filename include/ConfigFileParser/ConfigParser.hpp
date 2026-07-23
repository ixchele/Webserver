#pragma once

#include "CommonConfig.hpp"
#include "LocationConfig.hpp"
#include <ServerConfig.hpp>
#include <Token.hpp>
#include <deque>
#include <map>
#include <stdexcept>
#include <vector>


#include <HttpMethod.hpp>


class	ConfigParser {
	private:
		typedef void (ConfigParser::*MemFunc)();

		std::map<std::string, MemFunc> serverDirectives;
		std::map<std::string, MemFunc> locationDirectives;
		std::map<std::string, MemFunc> communDirectives;
		std::map<std::string, int>	methodsAvailable;

		TokenList					tokenList;
		TokenList::const_iterator	it;

		ServerConfig					*tmpServer;
		LocationConfig					tmpLocation;
		CommonConfig					*currentBlock;
		std::vector<ServerConfig*>		servers;

		// NOTE : Helpers
		bool		peek(const std::string &expected);
		void		consume(const std::string &expected);
		std::string	currentContent() const;
		// NOTE : Parsing functions

		// Dispatchers
		void        config();
		void        serverDirective();
		void        serverBlock();
		void        locationDirective();
		// Server directives
		void        listenDir();
		void        hostDir();
		void        nameDir();
		void        errorPageDir();
		void        locationBlock();
		// Location directives
		void        methodsDir();
		void        returnDir();
		void        uploadDir();
		// Commun directives
		void        rootDir();
		void        indexDir();
		void        autoindexDir();
		void        clientBodyDir();
		void		cgiPass();

	public:

		// NOTE : Exception struct
		struct ConfigException : public std::runtime_error {
			ConfigException(const std::string& error, const Token& token)
				: std::runtime_error(formatError(error, token)) {}

			ConfigException(const std::string& error)
				: std::runtime_error("[x] Configuration Error: " + error) {}

			static std::string formatError(const std::string& error, const Token& t) {
				std::stringstream ss;
				ss << "[x] Configuration Error [Line " << t.line << ", Col " << t.column << "]: " 
					<< error << " (Found: '" << t.content << "')";
				return ss.str();
			}
		};

		ConfigParser(const TokenList &tokenList);
		~ConfigParser(void);

		const std::vector<ServerConfig*>	parse();
};

std::ostream& operator<<(std::ostream& os, const CommonConfig& conf);
