#pragma once
#include <iostream>
#include <string>

template	<typename Container>
void	PrintContainer(const Container &container, const std::string	&label="Container") {
	std::cout << label << ": [";

	for (typename Container::const_iterator it = container.begin(); it != container.end(); ++it) {
		std::cout << "\"" << *it << "\"";	
		typename Container::const_iterator	nextIt = it;
		if (++nextIt != container.end())
			std::cout << ", ";
	}

	std::cout << " ]" << std::endl;
}

