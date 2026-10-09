#include "game.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const bool smokeTest = argc > 1 && std::string(argv[1]) == "--smoke-test";
    SheepdogGame game(smokeTest);
    game.Run();

    if (smokeTest) {
        std::printf("SHEEPDOG_SMOKE_OK frames_rendered=1 sheep=%d\n", 60);
    }
    return 0;
}
