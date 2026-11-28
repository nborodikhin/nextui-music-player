#include "ui/settings_page.hpp"

#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "album_art.h"
#include "downloader.h"
#include "error.h"
#include "selfupdate.h"
#include "settings.h"
#include "ui/about_page.hpp"
#include "ui/confirm_dialog_page.hpp"
#include "ui/errors_page.hpp"
#include "ui/page/menu_page.hpp"
#include "ui/wifi_dialog_page.hpp"
#include "ui/ytdlp_install_page.hpp"

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

/**
 * The rows of Settings. The id of a row is its SettingItem. A row with a value changes it with
 * LEFT and RIGHT, and each other row opens its action.
 */
class SettingsMenuAdapter final : public MenuAdapter {
public:
    /** Called with the item of a row with an action that the user selects */
    std::function<void(SettingItem)> onOpen;

protected:
    std::vector<long> loadRowIds() const override {
        std::vector<long> ids;
        if (Error_hasReports()) ids.push_back(static_cast<long>(SettingItem::Errors));
        for (const SettingRow& row : kRows) ids.push_back(static_cast<long>(row.item));
        return ids;
    }

    MenuRow loadRow(long id) const override {
        const SettingRow& setting = getSettingRow(static_cast<SettingItem>(id));
        MenuRow row{
            .id    = id,
            .label = setting.getLabel(),
        };
        if (setting.cycle) {
            row.value    = setting.getValue();
            row.onChange = setting.cycle;
        } else {
            row.onSelect = [this, item = setting.item] { onOpen(item); };
        }
        return row;
    }

private:
    static const SettingRow& getSettingRow(SettingItem item) {
        for (const SettingRow& row : kRows) {
            if (row.item == item) return row;
        }
        return kErrorsRow;
    }
};

class SettingsPageImpl final : public MenuPageImpl<SettingsPage> {
public:
    SettingsPageImpl() : MenuPageImpl("Settings", HELP_SETTINGS) {
        rows.onOpen = [this](SettingItem item) { openItem(item); };
    }

    void onEnter() override {
        start(rows);
        getScheduler().postRepeating([this] { rows.reload(); }, kLabelRefreshMs);
    }

    /** A page over Settings can change the labels: the size of the cache, the helpers, the update */
    void resume() override { rows.reload(); }

private:
    void openItem(SettingItem item) {
        switch (item) {
            case SettingItem::ClearCache:
                getNavigator().openForResult(ConfirmDialogPage("Clear album art cache?"), [this](const bool& yes) {
                    if (yes) album_art_clear_disk_cache();
                    rows.reload();
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

    SettingsMenuAdapter rows;
};

}  // namespace

std::unique_ptr<UiPageImplBase> SettingsPage::createPageImpl() const {
    return std::make_unique<SettingsPageImpl>();
}
