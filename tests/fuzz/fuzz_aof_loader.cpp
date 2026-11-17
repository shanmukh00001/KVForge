#include "persistence/aof.hpp"
#include "commands/command_table.hpp"
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <fcntl.h>

using namespace kvforge;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    char tmp_path[] = "/tmp/kvf_fuzz_aof_XXXXXX";
    int fd = ::mkstemp(tmp_path);
    if (fd < 0) return 0;

    if (size > 0) {
        ssize_t written = ::write(fd, data, size);
        (void)written;
    }
    ::close(fd);

    ServerContext ctx;
    std::string err;
    // Core invariant: must never crash, throw uncaught exceptions, or infinite loop
    AOF::load_and_replay(tmp_path, ctx, err);

    ::unlink(tmp_path);
    return 0;
}
