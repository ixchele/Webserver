<config>            ::= { <server_block> }

(* --- PRINCIPAL BLOCKS --- *)
<server_block>      ::= "server" "{" { <server_content> } "}"

<location_block>    ::= "location" <path> "{" { <location_content> } "}"

(* --- DISPATCHERS --- *)
<server_content>    ::= <server_only_dir> | <common_directives> | <location_block>

<location_content>  ::= <location_only_dir> | <common_directives>

(* ---  COMMON DIRECTIVES --- *)
<common_directives> ::= <root_dir>
                      | <index_dir>
                      | <autoindex_dir>
                      | <client_body_dir>
                      | <cgi_pass_dir>
                      | <error_page_dir>
                      | <return_dir>

(* --- EXCLUSIVE --- *)
<server_only_dir>   ::= <listen_dir> | <host_dir> | <name_dir>

<location_only_dir> ::= <methods_dir> | <upload_path_dir> | <upload_enable_dir>

(* --- DIRECTIVES --- *)
<listen_dir>        ::= "listen" <port_number> ";"
<host_dir>          ::= "host" ( <ip_address> | <string> ) ";"
<name_dir>          ::= "server_name" <string> { <string> } ";"
<error_page_dir>    ::= "error_page" <int> { <int> } <string> ";"
<client_body_dir>   ::= "client_max_body_size" <size_value> ";"
<root_dir>          ::= "root" <string> ";"
<index_dir>         ::= "index" <string> { <string> } ";"
<autoindex_dir>     ::= "autoindex" ("on" | "off") ";"
<methods_dir>       ::= "allow_methods" <method_name> { <method_name> } ";"
<return_dir>        ::= "return" <int> <string> ";"
<upload_enable_dir> ::= "upload_enable" ("on" | "off") ";"
<upload_path_dir>   ::= "upload_path" <string> ";"
<cgi_pass_dir>      ::= "cgi_pass" <string> <string> ";"

(* --- TERMINALS --- *)
<string>            ::= <char> { <char> }
<int>               ::= <digit> { <digit> }
<path>              ::= "/" { <char> }
<port_number>       ::= <int>
<ip_address>        ::= <int> "." <int> "." <int> "." <int>
<size_value>        ::= <int> [ "K" | "M" | "G" | "k" | "m" | "g" ]
<method_name>       ::= "HEAD" | "GET" | "POST" | "DELETE"

<digit>             ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
<char>              ::= <letter> | <digit> | "_" | "-" | "." | "/"
<letter>            ::= "a" ... "z" | "A" ... "Z"

(* ========================================================================= *)
(* --- DIRECTIVES REQUIREMENTS & PRIORITIES (POST-PARSING VALIDATION) ---    *)
(* ========================================================================= *)
(*
   🔴 MANDATORY (Absolute - Throw Error if missing)
   - <listen_dir>        : Required in <server_block> (For socket creation).

   🟡 CONDITIONAL & INHERITABLE (Required based on context)
   - <root_dir>          : Must exist at least at the <server_block> level 
                            OR be redefined in ALL <location_block>s.
   - <upload_path_dir>   : Mandatory IF <upload_enable_dir> is set to "on".

   🟢 OPTIONAL (Managed via default values in constructors)
   - <host_dir>          : Default = "0.0.0.0" (Listens on all interfaces if omitted).
   - <name_dir>          : Default = "". (Responds to the first domain by default).
   - <client_body_dir>   : Default = 1M (1048576 bytes).
   - <index_dir>         : Default = "index.html". (Inherits from server if inside a location).
   - <autoindex_dir>     : Default = "off". (Inherits from server if inside a location).
   - <error_page_dir>    : Default = The C++ server generates hardcoded HTML pages (404, 500).
   - <methods_dir>       : Default = "GET" allowed everywhere.
   - <return_dir>        : Default = "". (No redirection).
   - <upload_enable_dir> : Default = "off".
   - <cgi_pass_dir>      : Default = Empty map. (No CGI executed unless specified).
*)
(* ========================================================================= *)
