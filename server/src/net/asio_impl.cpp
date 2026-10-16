// Compile standalone Asio's implementation into a single translation unit.
//
// The whole project is built with ASIO_SEPARATE_COMPILATION (see CMakeLists.txt),
// which is the canonical way to avoid Asio's header-only multi-TU ODR problem:
// in header-only mode every TU that includes <asio.hpp> gets its own copy of
// Asio's namespace-scope/static state (service_registry, win_mutex, the winsock
// init guard, ...). When an io_context is constructed in one TU and a socket in
// another, the two copies disagree and EnterCriticalSection() runs on garbage —
// which showed up here as a SIGILL inside asio::detail::win_mutex::lock().
//
// With ASIO_SEPARATE_COMPILATION all TUs share the symbols compiled from this
// one file instead.
#include <asio/impl/src.hpp>
