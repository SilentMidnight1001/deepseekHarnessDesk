#include <wtlFileManagement.hpp>

#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc == 3 && std::strcmp(argv[1], "remove-tree") == 0) {
        const fs::path target = fs::u8path(argv[2]);
        if (!fs::exists(target)) {
            return 0;
        }

        wtl::FileManagement files;
        return files.removeDir(target.u8string()) ? 0 : 1;
    }

    if (argc == 3 && std::strcmp(argv[1], "create-tree") == 0) {
        wtl::FileManagement files;
        return files.createDir(argv[2]) ? 0 : 1;
    }

    std::cerr
        << "Usage: dsh-wintools-helper remove-tree <path>\n"
        << "       dsh-wintools-helper create-tree <path>\n";
    return 2;
}
