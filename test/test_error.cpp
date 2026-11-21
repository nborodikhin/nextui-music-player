// The error store: the capacity of a class, a repeated text, the classes, UTF-8 and the init.

#include "test.h"
#include "error.h"
#include "toast.h"

// The store shows its toast through the toast module, which needs a display. The test counts
// the toasts.
static int toastCount = 0;

ToastToken Toast_show(const char* msg, uint32_t duration_ms) {
    (void)msg;
    (void)duration_ms;
    toastCount++;
    return 1;
}

TEST(a_class_keeps_the_first_texts_up_to_its_capacity) {
    Error_init(2);
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/a");
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/b");
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/c");
    const std::vector<std::string> reports = Error_getReports(ERROR_CLASS_FILE_SYSTEM);
    CHECK(reports.size() == 2);
    CHECK(reports[0] == "/a");
    CHECK(reports[1] == "/b");
    Error_quit();
}

TEST(a_text_reported_twice_is_kept_once) {
    Error_init(3);
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/a");
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/a");
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/b");
    const std::vector<std::string> reports = Error_getReports(ERROR_CLASS_FILE_SYSTEM);
    CHECK(reports.size() == 2);
    CHECK(reports[1] == "/b");
    Error_quit();
}

TEST(the_classes_are_separate) {
    Error_init(1);
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/a");
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/b");
    Error_report(ERROR_CLASS_DATABASE, "step: disk I/O error");
    CHECK(Error_getReports(ERROR_CLASS_FILE_SYSTEM).size() == 1);
    const std::vector<std::string> database = Error_getReports(ERROR_CLASS_DATABASE);
    CHECK(database.size() == 1);
    CHECK(database[0] == "step: disk I/O error");
    Error_quit();
}

TEST(a_text_that_is_not_utf8_is_kept_as_utf8) {
    Error_init(1);
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/Music/Caf\xe9");
    const std::vector<std::string> reports = Error_getReports(ERROR_CLASS_FILE_SYSTEM);
    CHECK(reports.size() == 1);
    CHECK(reports[0] == "/Music/Caf\xc3\xa9");
    Error_quit();
}

TEST(a_report_before_the_init_does_nothing) {
    Error_quit();
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/a");
    CHECK(!Error_hasReports());
    Error_init(1);
    CHECK(!Error_hasReports());
    Error_report(ERROR_CLASS_DATABASE, "x");
    CHECK(Error_hasReports());
    Error_quit();
    CHECK(!Error_hasReports());
}

TEST(the_toast_shows_one_time_after_an_error) {
    Error_init(1);
    toastCount = 0;
    Error_showToast();
    CHECK(toastCount == 0);
    Error_report(ERROR_CLASS_FILE_SYSTEM, "/a");
    Error_showToast();
    Error_showToast();
    CHECK(toastCount == 1);
    Error_quit();
}

int main(void) {
    RUN(a_class_keeps_the_first_texts_up_to_its_capacity);
    RUN(a_text_reported_twice_is_kept_once);
    RUN(the_classes_are_separate);
    RUN(a_text_that_is_not_utf8_is_kept_as_utf8);
    RUN(a_report_before_the_init_does_nothing);
    RUN(the_toast_shows_one_time_after_an_error);
    return test_summary();
}
