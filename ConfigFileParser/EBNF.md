<!-- NOTE : EBNF STRUCT -->

<config>            ::= { <server_block> }

<server_block>      ::= "server" "{" { <server_directive> } "}"

<server_directive>  ::= <listen_dir> 
                      | <host_dir> 
                      | <name_dir> 
                      | <error_page_dir> 
                      | <client_body_dir> 
                      | <location_block>

<listen_dir>        ::= "listen" <port_number> ";"
<host_dir>          ::= "host" <ip_address> ";"
<name_dir>          ::= "server_name" <string> { <string> } ";"
<error_page_dir>    ::= "error_page" <int> <string> ";"
<client_body_dir>   ::= "client_max_body_size" <size_value> ";"

------------------------------------------

<location_block>     ::= "location" <path> "{" { <location_directive> } "}"

<location_directive> ::= <methods_dir> 
                       | <root_dir> 
                       | <index_dir> 
                       | <autoindex_dir> 
                       | <return_dir> 
                       | <upload_dir> 
                       | <cgi_pass_dir>

<methods_dir>        ::= "methods" <method_name> { <method_name> } ";"
<root_dir>           ::= "root" <string> ";"
<index_dir>          ::= "index" <string> ";"
<autoindex_dir>      ::= "autoindex" ("on" | "off") ";"
<return_dir>         ::= "return" <int> <string> ";"
<upload_dir>         ::= "upload_path" <string> ";"
<cgi_pass_dir>       ::= "cgi_extension" <string> | "cgi_path" <string> ";"
