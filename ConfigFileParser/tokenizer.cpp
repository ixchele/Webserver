#include <ConfigFile.hpp>
// #include <Token.hpp>

std::deque<Token> tokenizer(const std::string &confContent) {
    std::string       delim("{};");
    std::deque<Token> tokenList;
    std::string       word;

    int startCol = 1;
    int line = 1;
    int col = 1;

    for (std::size_t i = 0; i < confContent.size(); ++i) {
        char c = confContent[i];

        if (c == '#') {
            if (!word.empty()) {
                tokenList.push_back(Token(word, line, startCol));
                word.clear();
            }
            while (i < confContent.size() && confContent[i] != '\n')
                i++;
            if (i < confContent.size())
                i--; 
            continue;
        }

        if (c == '\n') {
            if (!word.empty()) 
                tokenList.push_back(Token(word, line, startCol));
            word.clear();
            line++;
            col = 1;
            continue;
        }

        if (delim.find(c) != std::string::npos) {
            if (!word.empty())
                tokenList.push_back(Token(word, line, startCol));

            tokenList.push_back(Token(std::string(1, c), line, col));
            word.clear();
        }
        else if (std::isspace(c)) {
            if (!word.empty())
                tokenList.push_back(Token(word, line, startCol));
            word.clear();
        }
        else {
            if (word.empty())
                startCol = col;
            word += c;
        }
        col++;
    }

    return tokenList;
}


std::ostream& operator<<(std::ostream& os, const Token& t) {
    os << "Token[" << t.line << ":" << t.column << "] -> \"" << t.content << "\"" << std::endl;
    return os;
}
