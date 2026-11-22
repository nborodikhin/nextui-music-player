#include "ui/radio_page.hpp"

#include <memory>
#include <string>
#include <unordered_map>

#include "background.h"
#include "radio.h"
#include "toast.h"
#include "ui/confirm_dialog_page.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/radio_manage_page.hpp"
#include "ui/radio_playing_page.hpp"
#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui/wifi_dialog_page.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

constexpr const char* kLeftHints[]       = {"START", "CONTROLS", nullptr};
constexpr const char* kRightHints[]      = {"B", "BACK", "A", "PLAY", nullptr};
constexpr const char* kEmptyRightHints[] = {"Y", "MANAGE", "B", "BACK", nullptr};

/** The URL of the station of the cursor when the station list closed last */
std::string lastSelectedUrl;

/**
 * The rows of the saved stations. The id of a row comes from the URL, the name and the genre of its
 * station, thus a station that does not change keeps its row view while the list shows.
 */
class StationListAdapter final : public ListAdapter {
public:
    int getCount() const override {
        RadioStation* stations;
        return Radio_getStations(&stations);
    }

    long getItemId(int index) const override {
        RadioStation* stations;
        Radio_getStations(&stations);
        const RadioStation& station = stations[index];
        std::string key = std::string(station.url) + '\n' + station.name + '\n' + station.genre;
        const auto [it, added] = ids.try_emplace(std::move(key), static_cast<long>(ids.size()));
        return it->second;
    }

    std::shared_ptr<ListItemView> getView(int index) override {
        RadioStation* stations;
        Radio_getStations(&stations);
        const RadioStation& station = stations[index];
        auto row = std::make_shared<ListRowView>(Fonts_getMedium());
        row->setText(station.name);
        if (station.genre[0]) {
            auto genre = std::make_shared<TextView>(Fonts_getTiny(), false);
            genre->setText(station.genre);
            genre->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
            row->setRightView(std::move(genre));
        }
        return row;
    }

private:
    mutable std::unordered_map<std::string, long> ids;
};

class RadioPageImpl final : public UiPageImpl<RadioPage>, private ListScreenListener {
public:
    RadioPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screenView.getTitleView().setText("Online Radio");
        screenView.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        emptyState.setMessage("No stations saved");
        emptyState.setLine("Press Y to manage stations");
        screenView.setEmptyState(&emptyState);
        for (TextView* note : {&noteFirst, &noteSecond}) note->setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        noteFirst.setText("These are default stations");
        noteSecond.setText("Press Y to manage stations");
    }

    std::span<const ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    HelpId getHelpId() const override { return HELP_RADIO_LIST; }

    void onEnter() override {
        if (Radio_init() != 0) {
            Toast_show("Online Radio is not available", TOAST_DURATION);
            close();
            return;
        }
        radioReady = true;
        screenView.getListView().setAdapter(stations);
        list.start();
        selectStation(lastSelectedUrl);
        updateHints();
        if (Background_getActive() == BG_RADIO && Radio_isActive()) openPlayingScreen({});
    }

    void onExit() override {
        if (!radioReady) return;
        int index;
        if (const RadioStation* station = getSelectedStation(index)) lastSelectedUrl = station->url;
        if (!Radio_isActive()) Radio_quit();
    }

    void layout(UiLayoutContext& context) override {
        context.layoutChild(screenView, context.getMaxRect());
        if (Radio_hasUserStations() || stations.getCount() == 0) return;
        // The note ends one pill row above the row of the button hints
        const int hintsTop    = context.getMaxSize().h - SCALE1(PADDING + PILL_SIZE);
        const int secondY     = hintsTop - SCALE1(PILL_SIZE);
        const int firstHeight = context.createMeasuringContext().layoutChild(noteFirst, context.getMaxRect()).getSize().h;
        layoutCentered(context, noteFirst, secondY - firstHeight);
        layoutCentered(context, noteSecond, secondY);
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (list.onButtonEvent(event)) return true;
        if (event.isSingleButton(Button::A)) {
            playSelectedStation();
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        if (event.isSingleButton(Button::Y)) {
            getNavigator().openForResult(RadioManagePage{}, [this](const std::monostate&) { reloadStations(); });
            return true;
        }
        if (event.isSingleButton(Button::X)) {
            confirmRemoval();
            return true;
        }
        return false;
    }

private:
    /** Returns the station of the selected row, or nullptr with no station. */
    const RadioStation* getSelectedStation(int& index) {
        index = screenView.getListView().getSelectedIndex();
        RadioStation* all;
        const int count = Radio_getStations(&all);
        return index >= 0 && index < count ? &all[index] : nullptr;
    }

    void playSelectedStation() {
        int index;
        const RadioStation* station = getSelectedStation(index);
        if (!station) return;
        std::string url = station->url;
        getNavigator().openForResult(WifiDialogPage{}, [this, url = std::move(url)](const bool& connected) {
            if (connected) openPlayingScreen(url);
        });
    }

    /** Opens the playing screen, and puts the cursor on the station that it showed when it closes. */
    void openPlayingScreen(std::string url) {
        getNavigator().openForResult(RadioPlayingPage{std::move(url)},
                                     [this](const std::string& shownUrl) { selectStation(shownUrl); });
    }

    void selectStation(const std::string& url) {
        RadioStation* all;
        const int count = Radio_getStations(&all);
        for (int index = 0; index < count; index++) {
            if (url != all[index].url) continue;
            screenView.getListView().setSelectedIndex(index);
            // The ids stay, thus the list keeps the new selection and shows it
            list.notifyDataChanged();
            return;
        }
    }

    void confirmRemoval() {
        int index;
        const RadioStation* station = getSelectedStation(index);
        if (!station) return;
        // The list does not change while the dialog shows, thus the index names the station of the dialog
        getNavigator().openForResult(ConfirmDialogPage("Remove Station?", station->name), [this, index](const bool& yes) {
            if (!yes) return;
            if (Radio_removeStation(index)) {
                Radio_saveStations();
                Toast_show("Removed", TOAST_DURATION);
            }
            reloadStations();
        });
    }

    /** Reads the stations again. The cursor keeps its position, or goes to the last row. */
    void reloadStations() {
        ListView& view     = screenView.getListView();
        const int position = view.getSelectedIndex();
        view.notifyDataChanged();
        if (position >= 0) view.setSelectedIndex(position);
        // The ids stay, thus the list keeps the selection and shows it
        list.notifyDataChanged();
        updateHints();
    }

    void updateHints() {
        screenView.getButtonHintsView().setHints(kLeftHints, stations.getCount() > 0 ? kRightHints : kEmptyRightHints);
    }

    StationListAdapter stations;
    ListScreenView screenView{Fonts_getLarge()};
    EmptyStateView emptyState;
    TextView noteFirst{Fonts_getTiny(), true};
    TextView noteSecond{Fonts_getTiny(), true};
    ListScreenController list{screenView, getScheduler(), *this};
    bool radioReady = false;
};

}  // namespace

std::unique_ptr<UiPageImplBase> RadioPage::createPageImpl() const {
    return std::make_unique<RadioPageImpl>();
}
