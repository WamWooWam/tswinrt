
#include <iostream>
#include <winmd_reader.h>
#include <winrt/base.h>
#include <wrl.h>
#include <Windows.ApplicationModel.h>
#include "interop/interop.h"
#include "writer.h"

using namespace winmd::reader;
using namespace ABI::Windows::ApplicationModel;

int main(int argc, char const *argv[]) {
    winrt::init_apartment();
    // winrt::Windows::ApplicationModel::DesignMode::DesignModeEnabled();

    std::vector<std::string> args;
    for (size_t i = 1; i < argc; i++) {
        args.push_back(std::string(argv[i]));
    }

    writer writer{ args, std::filesystem::current_path().append("output") };
    writer.write();

    return 0;
}
