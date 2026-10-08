#include <format>
#include <iostream>

int main() {
    std::cout << std::format("C++ version macro: {}\n", __cplusplus);
}

