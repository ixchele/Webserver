# --- Colors ---
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[0;33m
BOLD = \033[1m
RESET = \033[0m
CLEAR = \033[2K\r

# --- Variables ---
NAME = webserv
CXX = c++
INC_DIRS := $(shell find include -type d)
INCLUDES := $(addprefix -I, $(INC_DIRS))
CPPFLAGS = -Wall -Wextra -Werror -std=c++98 -g3 $(INCLUDES)
LFLAGS =  $(CPPFLAGS)
SRC = ./src/ConfigFileParser/Tokenizer/tokenizer.cpp \
		./main.cpp \
		./src/Logger/Logger.cpp  \
		./src/ConfigFileParser/ConfigStructures/CommonConfig.cpp \
		./src/ConfigFileParser/ConfigStructures/LocationConfig.cpp \
		./src/ConfigFileParser/ConfigStructures/ServerConfig.cpp \
		./src/ConfigFileParser/ConfigParser.cpp \
		./src/network/AFd.cpp ./src/network/Server.cpp ./src/network/Client.cpp \
		./src/network/Multiplexer.cpp ./src/network/Epoll.cpp \
		./src/http/HttpRequest.cpp ./src/http/RequestHandler.cpp ./src/http/Uri.cpp \
		./src/http/HttpResponse.cpp ./src/http/HttpStatus.cpp ./src/cgi/Cgi.cpp

OBJ = $(SRC:%.cpp=obj/%.o)

all: $(NAME)
	@printf "$(GREEN)$(BOLD)$(NAME) done!$(RESET)\n"

$(NAME): $(OBJ)
	@printf "$(CLEAR)$(YELLOW)linking $(NAME)...$(RESET)\n"
	@$(CXX) $(OBJ) $(LFLAGS) -o $(NAME)

obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	@printf "[$(GREEN)$(BOLD) OK $(RESET)$(BOLD)]$(RESET) compiling $(BOLD)$@...$(RESET)$(CLEAR)"
	@$(CXX) -c $(CPPFLAGS) $< -o $@

clean:
	@printf "$(RED)$(BOLD)cleaning object files...\n"
	@rm -rf obj/

fclean: clean
	@printf "$(RED)$(BOLD)cleaning all...\n"
	@rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
.SECONDARY: $(OBJ)
