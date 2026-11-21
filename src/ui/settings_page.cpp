#include "ui/settings_page.hpp"

#include <array>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>

#include "album_art.h"
#include "downloader.h"
#include "error.h"
#include "selfupdate.h"
#include "settings.h"
#include "ui/about_page.hpp"
#include "ui/confirm_dialog_page.hpp"
#include "ui/errors_page.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui/wifi_dialog_page.hpp"
#include "ui/ytdlp_install_page.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

#ifdef DEBUG
#include "ui/input_test_page.hpp"
#endif

using namespace input;

namespace {

/** How often Settings reads its labels again. A check for an update can end with no input. */
constexpr uint32_t kLabelRefreshMs = 3000;

/** Returns the index of the next or the previous value of `values`, from the index of `current`. */
template <class Value, size_t Count>
size_t stepValue(const std::array<Value, Count>& values, Value current, size_t fallback, HorizontalDirection direction) {
    size_t index = fallback;
    for (size_t candidate = 0; candidate < Count; candidate++) {
        if (values[candidate] == current) index = candidate;
    }
    const size_t step = direction == HorizontalDirection::Right ? 1 : Count - 1;
    return (index + step) % Count;
}

// Auto Screen Off, in seconds. 0 is off.
constexpr std::array<int, 4> kScreenOffValues = {60, 90, 120, 0};
constexpr size_t kDefaultScreenOffIndex       = 0;

void cycleScreenOff(HorizontalDirection direction) {
    const int current = Settings_getInt(&SETTING_SCREEN_OFF_TIMEOUT);
    Settings_setInt(&SETTING_SCREEN_OFF_TIMEOUT,
                    kScreenOffValues[stepValue(kScreenOffValues, current, kDefaultScreenOffIndex, direction)]);
}

std::string getScreenOffValue() {
    switch (Settings_getInt(&SETTING_SCREEN_OFF_TIMEOUT)) {
        case 60:  return "60s";
        case 90:  return "90s";
        case 120: return "120s";
        case 0:   return "Off";
        default:  return "60s";
    }
}

// The bass filter, in Hz. 0 is off.
constexpr std::array<int, 6> kBassFilterValues = {0, 80, 100, 120, 150, 200};
constexpr size_t kDefaultBassFilterIndex       = 3;

void cycleBassFilter(HorizontalDirection direction) {
    const int current = Settings_getInt(&SETTING_BASS_FILTER_HZ);
    Settings_setInt(&SETTING_BASS_FILTER_HZ,
                    kBassFilterValues[stepValue(kBassFilterValues, current, kDefaultBassFilterIndex, direction)]);
}

std::string getBassFilterValue() {
    const int current = Settings_getInt(&SETTING_BASS_FILTER_HZ);
    if (current == 0) return "Off";
    char value[16];
    snprintf(value, sizeof(value), "%d Hz", current);
    return value;
}

// The levels of the soft limiter. The setting stores the index.
constexpr std::array<int, 4> kSoftLimiterValues = {0, 1, 2, 3};
constexpr size_t kDefaultSoftLimiterIndex       = 2;

void cycleSoftLimiter(HorizontalDirection direction) {
    const int current = Settings_getInt(&SETTING_SOFT_LIMITER);
    Settings_setInt(&SETTING_SOFT_LIMITER,
                    kSoftLimiterValues[stepValue(kSoftLimiterValues, current, kDefaultSoftLimiterIndex, direction)]);
}

std::string getSoftLimiterValue() {
    switch (Settings_getInt(&SETTING_SOFT_LIMITER)) {
        case 0:  return "Off";
        case 1:  return "Mild";
        case 2:  return "Medium";
        case 3:  return "Strong";
        default: return "Medium";
    }
}

// The update channels, in the order of the row
constexpr std::array<int, 3> kUpdateChannelValues = {UPDATE_CHANNEL_OFF, UPDATE_CHANNEL_STABLE, UPDATE_CHANNEL_BETA};
constexpr size_t kDefaultUpdateChannelIndex       = 1;

/** Changes the channel, and forgets the result of the last check. */
void cycleUpdateChannel(HorizontalDirection direction) {
    const int current = Settings_getEnum(&SETTING_UPDATE_CHANNEL);
    Settings_setEnum(&SETTING_UPDATE_CHANNEL,
                     kUpdateChannelValues[stepValue(kUpdateChannelValues, current, kDefaultUpdateChannelIndex, direction)]);
    SelfUpdate_forgetCheck();
}

std::string getUpdateChannelValue() {
    switch (Settings_getEnum(&SETTING_UPDATE_CHANNEL)) {
        case UPDATE_CHANNEL_OFF:  return "Off";
        case UPDATE_CHANNEL_BETA: return "Beta";
        default:                  return "Stable";
    }
}

/** Returns a size in bytes as text, in B, KB or MB. */
std::string formatSize(long bytes) {
    char text[32];
    if (bytes >= 1024 * 1024) {
        snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(text, sizeof(text), "%ld B", bytes);
    }
    return text;
}

enum class SettingItem : uint8_t {
    Errors,
    ScreenOff,
    BassFilter,
    SoftLimiter,
    UpdateChannel,
    ClearCache,
    UpdateYtdlp,
    About,
#ifdef DEBUG
    InputTest,
#endif
};

/** A row of Settings. A row with a value has `getValue` and `cycle`, and each other row has an action. */
struct SettingRow {
    SettingItem item;
    std::string (*getLabel)();
    std::string (*getValue)();
    void (*cycle)(HorizontalDirection direction);
};

constexpr SettingRow kRows[] = {
    {SettingItem::ScreenOff, [] { return std::string("Auto Screen Off"); }, getScreenOffValue, cycleScreenOff},
    {SettingItem::BassFilter, [] { return std::string("Bass Filter"); }, getBassFilterValue, cycleBassFilter},
    {SettingItem::SoftLimiter, [] { return std::string("Soft Limiter"); }, getSoftLimiterValue, cycleSoftLimiter},
    {SettingItem::UpdateChannel, [] { return std::string("Updates"); }, getUpdateChannelValue, cycleUpdateChannel},
    {SettingItem::ClearCache,
     [] { return "Clear Album Art (" + formatSize(album_art_get_cache_size()) + ")"; },
     nullptr,
     nullptr},
    {SettingItem::UpdateYtdlp,
     [] {
         return std::string(Downloader_isAvailable() ? "Update Youtube download helpers"
                                                     : "Install Youtube download helpers");
     },
     nullptr,
     nullptr},
    {SettingItem::About,
     [] { return std::string(SelfUpdate_getStatus().update_available ? "About (Update available)" : "About"); },
     nullptr,
     nullptr},
#ifdef DEBUG
    {SettingItem::InputTest, [] { return std::string("Input test"); }, nullptr, nullptr},
#endif
};

/** The first row while the error store holds an error */
constexpr SettingRow kErrorsRow = {SettingItem::Errors, [] { return std::string("Errors"); }, nullptr, nullptr};

/** The count of the items of Settings, each a value of SettingItem from 0 */
constexpr size_t kSettingItemCount = std::size(kRows) + 1;

constexpr const char* kLeftHints[]        = {"START", "CONTROLS", nullptr};
constexpr const char* kValueRightHints[]  = {"B", "BACK", "LEFT/RIGHT", "CHANGE", nullptr};
constexpr const char* kActionRightHints[] = {"B", "BACK", "A", "OPEN", nullptr};

/** The rows of Settings, and the view of each row. The view of a row with a value shows it at the right. */
class SettingsListAdapter final : public ListAdapter {
public:
    SettingsListAdapter() {
        for (const SettingRow& row : kRows) rows.push_back(&row);
    }

    /**
     * Shows the row of the errors first when `show` is true.
     * Returns: true when the rows changed
     */
    bool setErrorsRowShown(bool show) {
        const bool shown = !rows.empty() && rows.front()->item == SettingItem::Errors;
        if (show == shown) return false;
        if (show) {
            rows.insert(rows.begin(), &kErrorsRow);
        } else {
            rows.erase(rows.begin());
        }
        return true;
    }

    const SettingRow& getSettingRow(int index) const { return *rows[static_cast<size_t>(index)]; }

    int getCount() const override { return static_cast<int>(rows.size()); }
    long getItemId(int index) const override { return static_cast<long>(getSettingRow(index).item); }

    /** Each item keeps its view while Settings shows, thus a row keeps it when the rows change. */
    std::shared_ptr<ListItemView> getView(int index) override {
        const SettingRow& row       = getSettingRow(index);
        const size_t item           = static_cast<size_t>(row.item);
        std::shared_ptr<ListRowView>& view = rowViews[item];
        if (!view) {
            view = std::make_shared<ListRowView>(Fonts_getMedium());
            if (row.getValue) {
                auto value = std::make_shared<TextView>(Fonts_getMedium(), false);
                value->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
                view->setRightView(value);
                values[item] = std::move(value);
            }
        }
        return view;
    }

    ListRowView& getRow(int index) { return static_cast<ListRowView&>(*getView(index)); }

    /** Sets the value of the row at `index`, with the arrows of a selected row. */
    void setValue(int index, bool selected) {
        const SettingRow& row = getSettingRow(index);
        if (!row.getValue) return;
        getView(index);
        const std::string value = row.getValue();
        values[static_cast<size_t>(row.item)]->setText(selected ? "< " + value + " >" : value);
    }

private:
    std::vector<const SettingRow*> rows;
    std::array<std::shared_ptr<ListRowView>, kSettingItemCount> rowViews;
    std::array<std::shared_ptr<TextView>, kSettingItemCount> values;
};

class SettingsPageImpl final : public UiPageImpl<SettingsPage>, private ListScreenListener {
public:
    SettingsPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screenView.getTitleView().setText("Settings");
        screenView.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
    }

    std::span<const ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    HelpId getHelpId() const override { return HELP_SETTINGS; }

    void onEnter() override {
        rows.setErrorsRowShown(Error_hasReports());
        screenView.getListView().setAdapter(rows);
        refreshLabels();
        list.start();
        getScheduler().postRepeating([this] { refreshLabels(); }, kLabelRefreshMs);
    }

    void setContext(UiContext& context) override { screenView.setContext(context); }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    /** A page over Settings can change the labels: the size of the cache, the helpers, the update */
    void resume() override { refreshLabels(); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (list.onButtonEvent(event)) return true;
        if (event.isSingleButton(Button::A)) {
            activate(screenView.getListView().getSelectedIndex());
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        return false;
    }

private:
    /**
     * A row with a value changes its value once for each press, and ignores a held button. Each
     * other row is not handled, thus the list moves one page.
     */
    bool onLeftRight(int listItemPosition, HorizontalDirection direction, bool isRepeat) override {
        const SettingRow& row = rows.getSettingRow(listItemPosition);
        if (!row.cycle) return false;
        if (isRepeat) return true;
        row.cycle(direction);
        // A new update channel forgets the last check, thus the label of About can change
        refreshLabels();
        return true;
    }

    void onSelectionChanged(int listItemPosition) override {
        if (selectedIndex >= 0) rows.setValue(selectedIndex, false);
        selectedIndex = listItemPosition;
        if (selectedIndex < 0) return;
        rows.setValue(selectedIndex, true);
        const bool hasValue = rows.getSettingRow(selectedIndex).getValue != nullptr;
        screenView.getButtonHintsView().setHints(kLeftHints, hasValue ? kValueRightHints : kActionRightHints);
    }

    /** Shows or hides the row of the errors, and reads the label and the value of each row again. */
    void refreshLabels() {
        if (rows.setErrorsRowShown(Error_hasReports())) list.notifyDataChanged();
        for (int index = 0; index < rows.getCount(); index++) {
            const bool changed = rows.getRow(index).setText(rows.getSettingRow(index).getLabel());
            if (changed && index == selectedIndex) list.restartRowScrolling();
            rows.setValue(index, index == selectedIndex);
        }
    }

    void activate(int index) {
        if (index < 0) return;
        const SettingRow& row = rows.getSettingRow(index);
        // A also changes a value
        if (row.cycle) {
            onLeftRight(index, HorizontalDirection::Right, false);
            return;
        }
        switch (row.item) {
            case SettingItem::ClearCache:
                getNavigator().openForResult(ConfirmDialogPage("Clear album art cache?"), [this](const bool& yes) {
                    if (yes) album_art_clear_disk_cache();
                    refreshLabels();
                });
                break;
            case SettingItem::UpdateYtdlp:
                // The downloader starts before the network is up, thus it does not resume its queue
                Downloader_init();
                getNavigator().openForResult(WifiDialogPage{}, [this](const bool& connected) {
                    if (connected) getNavigator().open(YtdlpInstallPage{});
                });
                break;
            case SettingItem::About:       getNavigator().open(AboutPage{}); break;
            case SettingItem::Errors:      getNavigator().open(ErrorsPage{}); break;
#ifdef DEBUG
            case SettingItem::InputTest:   getNavigator().open(InputTestPage{}); break;
#endif
            case SettingItem::ScreenOff:
            case SettingItem::BassFilter:
            case SettingItem::SoftLimiter:
            case SettingItem::UpdateChannel: break;
        }
    }

    SettingsListAdapter rows;
    ListScreenView screenView{Fonts_getLarge()};
    ListScreenController list{screenView, getScheduler(), *this};
    /** The row of the cursor, as the list gave it last */
    int selectedIndex = -1;
};

}  // namespace

std::unique_ptr<UiPageImplBase> SettingsPage::createPageImpl() const {
    return std::make_unique<SettingsPageImpl>();
}
