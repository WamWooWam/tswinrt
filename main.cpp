
#include <iostream>

#include "winmd_reader.h"
#include "writer.h"
#include <wrl.h>

using namespace winmd::reader;

int main(int argc, char const *argv[])
{
    // Microsoft::WRL::Wrappers::RoInitializeWrapper init{ RO_INIT_MULTITHREADED };

    std::vector<std::string> args;
    for (size_t i = 1; i < argc; i++)
    {
        args.push_back(std::string(argv[i]));
    }

    writer writer{args, std::filesystem::current_path()};
    writer.write();
    
    return 0;
}
