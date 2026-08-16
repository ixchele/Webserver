#include <HttpStatus.hpp>

std::string HttpStatus::codeMessage(Code code)
	{
		switch (code)
		{
		case Continue:
			return "Continue";
		case SwitchingProtocols:
			return "Switching Protocols";

		case OK:
			return "OK";
		case Created:
			return "Created";
		case Accepted:
			return "Accepted";
		case NonAuthoritativeInformation:
			return "Non-Authoritative Information";
		case NoContent:
			return "No Content";
		case ResetContent:
			return "Reset Content";
		case PartialContent:
			return "Partial Content";

		case MultipleChoices:
			return "Multiple Choices";
		case MovedPermanently:
			return "Moved Permanently";
		case Found:
			return "Found";
		case SeeOther:
			return "See Other";
		case NotModified:
			return "Not Modified";
		case UseProxy:
			return "Use Proxy";
		case TemporaryRedirect:
			return "Temporary Redirect";
		case PermanentRedirect:
			return "Permanent Redirect";

		case BadRequest:
			return "Bad Request";
		case Unauthorized:
			return "Unauthorized";
		case PaymentRequired:
			return "Payment Required";
		case Forbidden:
			return "Forbidden";
		case NotFound:
			return "Not Found";
		case MethodNotAllowed:
			return "Method Not Allowed";
		case NotAcceptable:
			return "Not Acceptable";
		case ProxyAuthenticationRequired:
			return "Proxy Authentication Required";
		case RequestTimeout:
			return "Request Timeout";
		case Conflict:
			return "Conflict";
		case Gone:
			return "Gone";
		case LengthRequired:
			return "Length Required";
		case PreconditionFailed:
			return "Precondition Failed";
		case PayloadTooLarge:
			return "Payload Too Large";
		case URITooLong:
			return "URI Too Long";
		case UnsupportedMediaType:
			return "Unsupported Media Type";
		case RangeNotSatisfiable:
			return "Range Not Satisfiable";
		case ExpectationFailed:
			return "Expectation Failed";
		case UpgradeRequired:
			return "Upgrade Required";

		case InternalServerError:
			return "Internal Server Error";
		case NotImplemented:
			return "Not Implemented";
		case BadGateway:
			return "Bad Gateway";
		case ServiceUnavailable:
			return "Service Unavailable";
		case GatewayTimeout:
			return "Gateway Timeout";
		case HTTPVersionNotSupported:
			return "HTTP Version Not Supported";
		}

		return "Unknown";
	}
