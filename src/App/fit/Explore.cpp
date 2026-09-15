
#include <IO.h>
#include <Logging.h>
#include <Options.h>

#include <cstdio>
#include <fit/FIT.h>

namespace ST {

using namespace FIT;

int main(int argc, char const **argv)
{
    int arg_ix = parse_options(argc, argv);
    if (arg_ix >= argc) {
        fprintf(stderr, "Usage: fitexplore <filename>\n");
        exit(1);
    }
    FITFile fit_file;
    auto    contents_maybe = fit_file.read(argv[arg_ix]);
    if (!contents_maybe) {
        fprintf(stderr, "Error parsing FIT file: %s\n", tag(contents_maybe.error()));
        exit(1);
    }
    printf("Read `%s`\n", argv[arg_ix]);

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
        auto rec = rec_maybe.value().value();
        std::println("{}: mesg_num: {}", ix, tag(static_cast<mesg_num>(rec.mesg_number)));
        ++ix;
    }
    return 0;
}

}

int main(int argc, char const **argv)
{
    return ST::main(argc, argv);
}
