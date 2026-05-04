#include "ConfigFileParser.template.hpp"
#include <ConfigFileParser.hpp>

int	main(void) {
	TokenList list = toknizer("server {\n    listen 8080;\n    host 127.0.0.1;\n    server_name exemple.com;\n    client_max_body_size 1M;\n    \n    error_page 404 error_pages/404.html;\n\n    location / {\n        root ./www;\n        methods GET;\n        index index.html;\n        autoindex on;\n    }\n\n    location /uploads {\n        methods GET POST DELETE;\n        root ./www/uploads;\n        upload_path ./www/uploads/storage;\n    }\n\n    location .py {\n        methods GET POST;\n        cgi_path /usr/bin/python3;\n    }\n\n    location /ancienne-page {\n        return 301 /index.html;\n    }\n}\n\nserver {\n    listen 9090;\n    host 0.0.0.0;\n    server_name test.com;\n}");
	PrintContainer(list, "TokenList");
}
