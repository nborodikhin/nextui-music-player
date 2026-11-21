#include "error.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "toast.h"
#include "utf8.h"

namespace {

struct ErrorStore {
    std::mutex mutex;
    std::array<std::vector<std::string>, ERROR_CLASS_COUNT> reports;
    size_t capacity = 0;
};

ErrorStore store;

/** The toast for errors shows one time for the life of the process, only on the UI thread */
bool toastShown = false;

bool isValidClass(ErrorClass errorClass) {
    return errorClass >= 0 && errorClass < ERROR_CLASS_COUNT;
}

/** Returns `text` as UTF-8. */
std::string toUtf8(const char* text) {
    const size_t length = strlen(text);
    // A conversion from ISO-8859-1 gives at most two bytes for each byte.
    std::string converted(length * 2 + 1, '\0');
    const size_t written = UTF8_fromUnknown(converted.data(), converted.size(),
                                            reinterpret_cast<const uint8_t*>(text), length);
    converted.resize(written);
    return converted;
}

}  // namespace

void Error_init(int capacity) {
    std::lock_guard lock(store.mutex);
    for (auto& texts : store.reports) texts.clear();
    store.capacity = capacity > 0 ? static_cast<size_t>(capacity) : 0;
}

void Error_quit(void) {
    std::lock_guard lock(store.mutex);
    for (auto& texts : store.reports) texts.clear();
    store.capacity = 0;
}

void Error_report(ErrorClass error_class, const char* text) {
    if (!isValidClass(error_class) || !text) return;
    std::string converted = toUtf8(text);
    std::lock_guard lock(store.mutex);
    std::vector<std::string>& texts = store.reports[error_class];
    if (texts.size() >= store.capacity) return;
    if (std::find(texts.begin(), texts.end(), converted) != texts.end()) return;
    texts.push_back(std::move(converted));
}

std::vector<std::string> Error_getReports(ErrorClass errorClass) {
    if (!isValidClass(errorClass)) return {};
    std::lock_guard lock(store.mutex);
    return store.reports[errorClass];
}

bool Error_hasReports() {
    std::lock_guard lock(store.mutex);
    return std::any_of(store.reports.begin(), store.reports.end(),
                       [](const std::vector<std::string>& texts) { return !texts.empty(); });
}

void Error_showToast() {
    if (toastShown || !Error_hasReports()) return;
    toastShown = true;
    Toast_show("Error reading files, see Settings", TOAST_DURATION);
}
