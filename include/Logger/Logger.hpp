#ifndef LOGGER_HPP
# define LOGGER_HPP

# include <iostream>
# include <string>
# include <sstream>
# include <fstream>
# include <ctime>

class Logger {
	public:
		enum Level { DEBUG, INFO, WARNING, ERROR };

		static Logger& getInstance();

		void	write(Level level, const std::string &msg);
		void	setMinLevel(Level level);
		void	setLogFile(const std::string &filename);
		void	logToConsole(bool option);

	private:
		Logger();
		~Logger();

		std::ofstream	_fileOutput;	// NOTE : default : no log file
		Level			_minLevel;		// NOTE : default : DEBUG
		bool			_logToConsole;	// NOTE : default : true

		std::string	getLevelStr(Level level);
		std::string	getTimeStr();
};

class LogStream {
	private:
		Logger::Level		_msgLevel;
		std::ostringstream	_buffer;

	public:
		LogStream(Logger::Level level);
		~LogStream();

		template <typename T>
			LogStream&	operator<<(const T &data) {
				_buffer << data;
				return *this;
			}
};

# define LOG_DEBUG	LogStream(Logger::DEBUG)
# define LOG_INFO	LogStream(Logger::INFO)
# define LOG_WARN	LogStream(Logger::WARNING)
# define LOG_ERROR	LogStream(Logger::ERROR)

#endif
