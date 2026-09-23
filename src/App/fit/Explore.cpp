#include <cstdio>
#include <format>

#include <Date.h>
#include <IO.h>
#include <Logging.h>
#include <Options.h>

#include <fit/FIT.h>
#include <fit/Profile.h>

namespace ST {

using namespace FIT;

int main(int argc, char const **argv)
{
    int arg_ix = parse_options(argc, argv);
    if (arg_ix >= argc) {
        fprintf(stderr, "Usage: fitexplore <filename>\n");
        exit(1);
    }
    auto fit_file_maybe = FITFile::read(argv[arg_ix]);
    if (!fit_file_maybe) {
        fprintf(stderr, "Error parsing FIT file: %s\n", tag(fit_file_maybe.error()));
        exit(1);
    }
    printf("Read `%s`\n", argv[arg_ix]);

    auto  &fit_file = fit_file_maybe.value();
    size_t ix = 0;
    while (true) {
        auto rec_maybe = (ix == 0) ? fit_file.first() : fit_file.next();
        if (!rec_maybe) {
            printf("Error reading FIT message: %s\n", tag(rec_maybe.error()));
            exit(1);
        }
        if (!rec_maybe.value()) {
            printf("Read %zu messages\n", ix);
            break;
        }
        auto const &rec = rec_maybe.value().value();
        std::println("{}: {}", ix, rec);
        ++ix;
    }
    return 0;
}

}

int main(int argc, char const **argv)
{
    return ST::main(argc, argv);
}
