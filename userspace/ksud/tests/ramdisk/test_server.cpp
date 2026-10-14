#include "boot/ramdisk_editor.hpp"

#include <unistd.h>
#include <csignal>

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 2;
    return ksud::run_ramdisk_editor(argv[1], STDIN_FILENO, STDOUT_FILENO);
}
