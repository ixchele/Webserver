#include <Response.hpp>


HttpResponse::HttpResponse(HttpRequest *request) : _request(request)
{
}

HttpResponse::~HttpResponse()
{
}
