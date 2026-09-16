#pragma once

namespace raiden {

class Application {
public:
    Application(int argc, char** argv);
    void run();

private:
    const char* argv0_ = nullptr;
};

}  // namespace raiden
