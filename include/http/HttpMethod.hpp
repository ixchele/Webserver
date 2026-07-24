#ifndef HTTP_METHOD_HPP
#define HTTP_METHOD_HPP

enum HttpMethod {
	HTTP_UNKNOWN = 0,
	HTTP_HEAD    = 1,  // 1 << 0
	HTTP_GET     = 2,  // 1 << 1
	HTTP_POST    = 4,  // 1 << 2
	HTTP_DELETE  = 8   // 1 << 3
};

#endif
