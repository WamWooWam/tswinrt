
#include <iostream>
#include <winmd_reader.h>

#include "writer.h"

using namespace winmd::reader;

int main(int argc, char const *argv[])
{
    std::vector<std::string> args;
    for (size_t i = 1; i < argc; i++)
    {
        args.push_back(std::string(argv[i]));
    }

    writer writer{args, std::filesystem::current_path().append("output")};
    writer.write();
    
    return 0;
}
