#include <Logger.hpp>

Logger	&Logger::getInstance() {
	static Logger	instance;
	return instance;
}

Logger::Logger() : _minLevel(DEBUG), _logToConsole(true) {}

Logger::~Logger() {
	if (_fileOutput.is_open()) {
		_fileOutput.close();
	}
}

void	Logger::setMinLevel(Level level) {
	_minLevel = level;
}

void	Logger::setLogFile(const std::string& filename) {
	if (_fileOutput.is_open()) {
		_fileOutput.close();
	}

	_fileOutput.open(filename.c_str(), std::ios::app);
}

std::string	Logger::getLevelStr(Level level) {
	switch (level) {
		case DEBUG:		return "\033[36mDEBUG\033[0m";
		case INFO:		return "\033[32mINFO \033[0m";
		case WARNING:	return "\033[33mWARN \033[0m";
		case ERROR:		return "\033[31mERROR\033[0m";
		default:		return "UNKNO";
	}
}

std::string Logger::getTimeStr() {
	time_t		now = time(NULL);
	struct tm	tstruct;

	char	buf[80];
	tstruct = *localtime(&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tstruct);
	return std::string(buf);
}

void	Logger::write(Level level, const std::string &msg) {
	if (level < _minLevel) {
		return;
	}

	std::string	timeStr = getTimeStr();

	if (_logToConsole) {
		std::cerr << "[" << timeStr << "] [" << getLevelStr(level) << "] " << msg << std::endl;
	}

	if (_fileOutput.is_open()) {
		std::string rawLevel;
		switch(level) {
			case DEBUG:   rawLevel = "DEBUG"; break;
			case INFO:    rawLevel = "INFO "; break;
			case WARNING: rawLevel = "WARN "; break;
			case ERROR:   rawLevel = "ERROR"; break;
			default:      rawLevel = "UNKNO";
		}
		_fileOutput << "[" << timeStr << "] [" << rawLevel << "] " << msg << "\n";
		_fileOutput.flush();
	}
}

LogStream::LogStream(Logger::Level level) : _msgLevel(level) {}

LogStream::~LogStream() {
	Logger::getInstance().write(_msgLevel, _buffer.str());
}
