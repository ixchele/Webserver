<config>            ::= { <server_block> }

(* --- PRINCIPAL BLOCKS --- *)
<server_block>      ::= "server" "{" { <server_content> } "}"

<location_block>    ::= "location" <path> "{" { <location_content> } "}"

(* --- DISPATCHERS --- *)
<server_content>    ::= <server_only_dir> | <common_directives> | <location_block>

<location_content>  ::= <location_only_dir> | <common_directives>

(* ---  COMMUNE DIRECTIVES --- *)
<common_directives> ::= <root_dir>
                      | <index_dir>
                      | <autoindex_dir>
                      | <client_body_dir>
                      | <cgi_extension_dir>
                      | <cgi_path_dir>

(* --- EXCLUSIV --- *)
<server_only_dir>   ::= <listen_dir> | <host_dir> | <name_dir> | <error_page_dir>

<location_only_dir> ::= <methods_dir> | <return_dir> | <upload_dir>

(* --- DIRECTIVES --- *)
<listen_dir>        ::= "listen" <port_number> ";"
<host_dir>          ::= "host" <ip_address> ";"
<name_dir>          ::= "server_name" <string> { <string> } ";"
<error_page_dir>    ::= "error_page" <int> { <int> } <string> ";"
<client_body_dir>   ::= "client_max_body_size" <size_value> ";"
<root_dir>          ::= "root" <string> ";"
<index_dir>         ::= "index" <string> ";"
<autoindex_dir>     ::= "autoindex" ("on" | "off") ";"
<methods_dir>       ::= "methods" <method_name> { <method_name> } ";"
<return_dir>        ::= "return" <int> <string> ";"
<upload_dir>        ::= "upload_path" <string> ";"
<cgi_extension_dir> ::= "cgi_extension" <string> ";"
<cgi_path_dir>      ::= "cgi_path" <string> ";"
