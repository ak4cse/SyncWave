#include "CommandInterface.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        syncwave::CommandInterface app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown error encountered.\n";
        return 1;
    }
}
