#ifndef __UI_PAGE_STACK_HPP__
#define __UI_PAGE_STACK_HPP__

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "input/button_detector.hpp"
#include "input/common_button_handler.hpp"
#include "input/input_manager.hpp"
#include "ui/ui_page.hpp"
#include "ui/view/snapshot_view.hpp"
#include "ui/view/ui_compositor.hpp"

/**
 * Holds the open pages, gives the input to the top page, and applies the navigation commands at
 * the end of the frame. run() operates it in the frame loop of the app. The frame steps are
 * public, thus a host test can operate a stack with no display.
 */
class PageStack final : public PageNavigator {
public:
    explicit PageStack(input::PlaybackControl& playback);
    ~PageStack();

    PageStack(const PageStack&)            = delete;
    PageStack& operator=(const PageStack&) = delete;

    /**
     * Runs a stack with page at the bottom. Returns when the bottom page closes, or at a quit. A
     * stack can run inside a legacy module that a legacy page of another stack runs. Returns at
     * once when a stop signal came before the call.
     */
    static void run(const UiPage& page, DisplayContext* display);

    /**
     * Sets the functions that open the dialogs of the global buttons. openHelp gets the help id
     * of the top page, and the stack calls it only for a page that has controls help.
     */
    void setDialogOpeners(std::function<void(HelpId)> openHelp, std::function<void()> openQuit);

    /** Puts page at the bottom, and gives it its context and onEnter(). */
    void start(std::unique_ptr<UiPageImplBase> page, UiContext& context);

    /** Gives each page the context of a new screen. */
    void setContext(UiContext& context);

    /** Call before each poll of the pad. */
    void beginPoll();

    /** Returns the listener of the raw events of a poll. */
    input::RawEventListener& getRawInput() { return rawInput; }

    /** Gives the time of the frame to the detector of the buttons. */
    void tickInput(uint32_t now);

    /** Calls onTick() of the top page. */
    void tickTop(uint32_t now);

    /** Sets the color that a dialog puts over the page under it. */
    void setDimColor(SDL_Color color) { dimColor = color; }

    /**
     * Lays out the top page. A dialog shows over a dimmed snapshot of the page under it, thus
     * that page is not laid out while it is paused. The stack takes the snapshot when the dialog
     * becomes the top, and again after a new context. While a dialog is the top, the whole frame
     * goes on the regular surface.
     */
    void layout(UiCompositor& compositor);

    /** Applies the navigation command of the frame. */
    void endFrame(uint32_t now);

    /** Closes each page from the top down with no result. */
    void closeAll();

    bool isDone() const { return pages.empty(); }

    bool isDialogOnTop() const;

    /** Returns the top page, or nullptr when the stack is done. */
    UiPageImplBase* getTop() const { return pages.empty() ? nullptr : pages.back().get(); }

    input::CommonButtonHandler& getCommonButtonHandler() { return commonButtonHandler; }

    /** Returns true one time after the stack applied exit(). The caller sets the quit flag. */
    bool takeQuitRequest();

    /** Returns the legacy module that the top page asked to run, and forgets it. */
    std::function<ModuleExitReason(DisplayContext*)> takeLegacyModule();

    /** Closes the legacy page after its module returned, and resumes the page under it. */
    void completeLegacyModule(uint32_t now);

    void exit() override;
    void runLegacyModule(std::function<ModuleExitReason(DisplayContext*)> module) override;

protected:
    void queueOpen(std::unique_ptr<UiPageImplBase> page) override;
    void queueClose(UiPageImplBase& page, std::function<void()> deliver) override;

private:
    struct Command {
        enum class Kind : uint8_t { Open, Close, Exit };
        Kind kind;
        std::unique_ptr<UiPageImplBase> page;
        std::function<void()> deliver;
    };

    /** Gives each raw event to the input manager, and then to the detector. */
    class RawEventFanOut : public input::RawEventListener {
    public:
        explicit RawEventFanOut(PageStack& stack) : stack(stack) {}
        bool onRawEvent(const input::RawEvent& event) override;

    private:
        PageStack& stack;
    };

    /** Returns false and keeps no command when the frame already has one. */
    bool acceptCommand();
    void apply(Command command, uint32_t now);
    void push(std::unique_ptr<UiPageImplBase> page, uint32_t now);
    void pop(uint32_t now, std::function<void()> deliver);
    /** Gives the input to the new top page. */
    void connectTop();
    /** Takes the snapshot of the page under the top dialog. */
    void takeSnapshot(UiCompositor& compositor);

    input::CommonButtonHandler commonButtonHandler;
    input::InputManager manager;
    std::unique_ptr<input::ButtonDetector> detector;
    RawEventFanOut rawInput;
    std::vector<std::unique_ptr<UiPageImplBase>> pages;
    UiContext* context = nullptr;
    std::optional<Command> pending;
    /** The frame has a command, thus the stack ignores a later one */
    bool frameHasCommand = false;
    bool quitRequested   = false;
    std::function<ModuleExitReason(DisplayContext*)> legacyModule;
    std::function<void(HelpId)> openHelp;
    std::function<void()> openQuit;
    uint32_t frameTime = 0;
    SnapshotView snapshot;
    /** The snapshot does not show the page under the top dialog */
    bool snapshotStale = true;
    SDL_Color dimColor{};
};

#endif
