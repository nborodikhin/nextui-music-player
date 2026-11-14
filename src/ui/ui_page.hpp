#ifndef __UI_UI_PAGE_HPP__
#define __UI_UI_PAGE_HPP__

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

#include "display_helper.h"
#include "help_screen.h"
#include "input/input_listeners.hpp"
#include "module_common.h"
#include "ui/view/ui_context.hpp"

class UiLayoutContext;
class UiPageImplBase;
class PageNavigator;
template <class Page>
class UiPageImpl;

/*
 * The life cycle of a page.
 *
 * A caller opens a page through its declaration, a subclass of UiPage, and the navigator. The
 * declaration holds the parameters of the page and the type of its result. Its implementation, a
 * subclass of UiPageImpl, is hidden in the .cpp file of the page.
 *
 * The stack calls the callbacks of an implementation in this order:
 * 1. setContext() and then onEnter(), one time, when the page becomes the top for the first time.
 *    Both come before the first layout of the page. setContext() comes again at each new screen.
 * 2. In each frame while the page is the top: the raw events and the button events, then
 *    onTick().
 * 3. pause() when another page covers it. A covered page gets no event and no tick. It can be
 *    visible, and the stack can lay it out.
 * 4. resume() when it becomes the top again.
 * 5. onExit(), one time, when the page closes or the stack ends.
 *
 * A navigation command takes effect at the end of the frame. After the first command of a frame,
 * the page gets no more events of that frame, and the stack ignores a later command of that
 * frame. When a page that was opened for a result closes, the stack destroys it, calls the
 * callback of the caller with the result, and then gives resume() to the page under it.
 */

/** How a page takes the screen. */
enum class PageKind : uint8_t {
    /** Takes the whole screen. The pages under it are not visible. */
    FullScreen,
    /** Shows over the page under it, which stays visible under a dim. */
    Dialog,
};

/**
 * The declaration of a page: the parameters of the page and the type of its result, as
 * `using Result = ...`. A page with no result uses std::monostate.
 */
class UiPage {
public:
    /**
     * Creates the implementation of this page. The implementation derives from UiPageImpl of the
     * same declaration, thus the navigator can give it the result callback.
     */
    virtual std::unique_ptr<UiPageImplBase> createPageImpl() const = 0;

protected:
    ~UiPage() = default;
};

/** The navigation commands that a page can give. Each one takes effect at the end of the frame. */
class PageNavigator {
public:
    /** Opens page over the top page. */
    void open(const UiPage& page);

    /** Opens page over the top page. Calls onResult with the result of page when it closes. */
    template <class Page>
    void openForResult(const Page& page,
                       std::function<void(const typename Page::Result&)> onResult);

    /** Quits the app: closes each page from the top down with no result, and ends the stack. */
    virtual void exit() = 0;

    /**
     * Runs a legacy module in its own frame loop, at the end of this frame. For the legacy pages
     * only. The page that calls it closes when the module returns, and MODULE_EXIT_QUIT from the
     * module quits the app.
     */
    virtual void runLegacyModule(std::function<ModuleExitReason(DisplayContext*)> module) = 0;

protected:
    ~PageNavigator() = default;

    virtual void queueOpen(std::unique_ptr<UiPageImplBase> page) = 0;

    /** Closes page, and then calls deliver when it is set. */
    virtual void queueClose(UiPageImplBase& page, std::function<void()> deliver) = 0;

    friend class UiPageImplBase;
};

/** The implementation of a page, hidden from its callers. */
class UiPageImplBase : public input::InputListener {
public:
    explicit UiPageImplBase(PageKind kind) : kind(kind) {}
    virtual ~UiPageImplBase() = default;

    UiPageImplBase(const UiPageImplBase&)            = delete;
    UiPageImplBase& operator=(const UiPageImplBase&) = delete;

    PageKind getKind() const { return kind; }

    /** Returns the long presses, the repeats and the combos that the page uses. */
    virtual std::span<const input::ButtonRequest> getButtonRequests() const { return {}; }

    virtual HelpId getHelpId() const { return HELP_NONE; }

    virtual void onEnter() {}
    virtual void onExit() {}

    bool onRawEvent(const input::RawEvent&) override { return false; }
    bool onButtonEvent(const input::ButtonEvent&) override { return false; }

    /** Called each frame after the events, with the time of the frame, while the page is the top. */
    virtual void onTick(uint32_t) {}

    /** Gives the views their context. */
    virtual void setContext(UiContext&) {}

    /** Lays out the views of the page in context, which is the whole screen. */
    virtual void layout(UiLayoutContext& context) = 0;

    /** Stops the clocks of the page at now, while a page covers it or the device sleeps. */
    virtual void pause(uint32_t) {}

    /** Continues the clocks of the page from now. */
    virtual void resume(uint32_t) {}

protected:
    /** Returns the navigator of the stack that holds the page. Valid from onEnter() to onExit(). */
    PageNavigator& getNavigator() const { return *navigator; }

    void requestClose(std::function<void()> deliver) {
        navigator->queueClose(*this, std::move(deliver));
    }

private:
    friend class PageStack;

    PageKind kind;
    PageNavigator* navigator = nullptr;
};

/** The base of the implementation of Page. */
template <class Page>
class UiPageImpl : public UiPageImplBase {
    using Result = typename Page::Result;
    static_assert(std::is_default_constructible_v<Result>);
    static_assert(std::is_copy_constructible_v<Result>);

public:
    explicit UiPageImpl(PageKind kind) : UiPageImplBase(kind) {}

protected:
    /** Closes the page. A page opened for a result gives Result{}. */
    void close() { finishWith(Result{}); }

    /** Closes the page and gives result to the page that opened it. */
    void finish(Result result)
        requires(!std::is_same_v<Result, std::monostate>)
    {
        finishWith(std::move(result));
    }

private:
    friend class PageNavigator;

    void finishWith(Result result) {
        if (!onResult) {
            requestClose(nullptr);
            return;
        }
        requestClose([callback = std::move(onResult), result = std::move(result)] { callback(result); });
    }

    std::function<void(const Result&)> onResult;
};

inline void PageNavigator::open(const UiPage& page) {
    queueOpen(page.createPageImpl());
}

template <class Page>
void PageNavigator::openForResult(const Page& page,
                                  std::function<void(const typename Page::Result&)> onResult) {
    std::unique_ptr<UiPageImplBase> impl = page.createPageImpl();
    static_cast<UiPageImpl<Page>&>(*impl).onResult = std::move(onResult);
    queueOpen(std::move(impl));
}

#endif
