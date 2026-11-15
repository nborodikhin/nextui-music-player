#ifndef __SCOPED_INIT_HPP__
#define __SCOPED_INIT_HPP__

#include <type_traits>
#include <utility>

/** The value of a guard whose init function returns nothing. */
struct ScopedInitNoValue {};

/**
 * Calls the quit function when the guard goes out of scope. A guard made with an init function
 * calls it first, and keeps its result in `value`.
 */
template <typename Quit, typename Value = ScopedInitNoValue>
class [[nodiscard]] ScopedInit {
public:
    explicit ScopedInit(Quit quit) : quit(std::move(quit)) {}

    template <typename Init>
    ScopedInit(Init init, Quit quit) : value(callInit(init)), quit(std::move(quit)) {}

    ~ScopedInit() { quit(); }

    ScopedInit(const ScopedInit&)            = delete;
    ScopedInit& operator=(const ScopedInit&) = delete;

    const Value value{};

private:
    template <typename Init>
    static Value callInit(Init& init) {
        if constexpr (std::is_void_v<std::invoke_result_t<Init&>>) {
            init();
            return {};
        } else {
            return init();
        }
    }

    Quit quit;
};

template <typename Init>
using ScopedInitValue = std::conditional_t<std::is_void_v<std::invoke_result_t<Init&>>, ScopedInitNoValue,
                                          std::invoke_result_t<Init&>>;

template <typename Quit>
ScopedInit(Quit) -> ScopedInit<Quit>;

template <typename Init, typename Quit>
ScopedInit(Init, Quit) -> ScopedInit<Quit, ScopedInitValue<Init>>;

#endif
