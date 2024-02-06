#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>

co_context::task<> cycle(int sec, const char *message) {
    while (true) {
        co_await co_context::timeout(std::chrono::seconds{sec});
        printf("%s\n", message);
    }
}

co_context::task<> cycle_abs(int sec, const char *message) {
    auto next = std::chrono::steady_clock::now();
    while (true) {
        next = next + std::chrono::seconds{sec};
        co_await co_context::timeout_at(next);
        printf("%s\n", message);
    }
}

int main() {
    co_context::config::set_log_level(co_context::config::level::info);
    co_context::io_context ctx;
    ctx.set_name("test");
    ctx.co_spawn(cycle(1, "1 sec"));
    ctx.co_spawn(cycle_abs(1, "1 sec [abs]"));
    ctx.co_spawn(cycle(3, "\t3 sec"));
    ctx.start();
    ctx.join();
    return 0;
}
