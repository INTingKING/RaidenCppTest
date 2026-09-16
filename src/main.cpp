#include "Application.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        raiden::Application app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
