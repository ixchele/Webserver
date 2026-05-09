#pragma once

//  NOTE : BNF
//
// <config>            ::= <server_block> { <server_block> }
//
// <server_block>      ::= "server" "{" { <server_directive> } "}"
//
// <server_directive>  ::= <listen> | <host> | <server_name> | <body_size>
// 						   | <error_page> | <location_block>
//
// <location_block>    ::= "location" <path> "{" { <location_directive> } "}"
//
// <location_directive> ::= <methods> | <root> | <index> | <autoindex>
// 						    | <return> | <upload_path> | <cgi_pass>
//
// <listen>            ::= "listen" <port_number> ";"
// <methods>           ::= "methods" <method_name> { <method_name> } ";"
// <error_page>        ::= "error_page" <error_code> <file_path> ";"


#include <string>
#include <vector>
#include <sstream>

typedef struct Token Token;
typedef std::vector<Token> TokenList;


TokenList	toknizer(const std::string &content);

#include <ConfigFileParser.template.hpp>
