#ifndef __INPUT_COMMON_BUTTON_HANDLER_HPP__
#define __INPUT_COMMON_BUTTON_HANDLER_HPP__

#include <cstdint>
#include <functional>
#include <span>

#include "input_listeners.hpp"
#include "input_state.hpp"

namespace input {

// Runs a playback command on the radio or on the player that is active. The page loop gives
// the platform version.
class PlaybackControl {
public:
    virtual void togglePlayback() = 0;
    // Pass true in next to go to the next track or station, false for the previous one
    virtual void skipTrack(bool next) = 0;
    virtual void changeVolume(int step) = 0;

protected:
    ~PlaybackControl() = default;
};

// The input that is the same on each page: power management, the buttons of USB earphones, and
// START. It consumes the pressed event of START, each Media event, and START with a volume button,
// which asks for no dialog.
class CommonButtonHandler : public InputListener {
public:
    explicit CommonButtonHandler(PlaybackControl& playback);

    // Sets the function that a short press of START calls. The host shows the controls help of the
    // page on the top.
    void setHelpRequestedCallback(std::function<void()> callback);

    // Sets the function that a long press of START calls. The host shows the Quit dialog.
    void setQuitRequestedCallback(std::function<void()> callback);

    // Returns the requests of the global buttons. The detector gets them before the requests of
    // the page.
    static std::span<const ButtonRequest> getButtonRequests();

    bool onRawEvent(const RawEvent& event) override;
    bool onButtonEvent(const ButtonEvent& event) override;

    // Runs power management for the frame, and writes the hardware bar to InputState.
    // Returns: true when the screen must draw again
    bool onTick(uint32_t now);

    // Returns true when the device woke from sleep in the last onTick(). The sleep cleared the
    // screen.
    bool hasWoken() const { return woke; }

private:
    PlaybackControl& playback;
    std::function<void()> onHelpRequested;
    std::function<void()> onQuitRequested;
    bool woke = false;
};

}  // namespace input

#endif
