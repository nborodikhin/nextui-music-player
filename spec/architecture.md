# Spec: Architecture

## Application Lifecycle

```
main() [musicplayer.c]
  ├── GFX_init()            // SDL2 + GLES surface
  ├── Fonts_load()          // TTF fonts
  ├── Splash screen render  // Immediate feedback before heavy init
  ├── InitSettings()        // NextUI msettings
  ├── PAD_init() / PWR_init() / WIFI_init()
  ├── psa_crypto_init()     // mbedTLS PSA layer
  ├── Icons_init()          // PNG icons
  ├── Player_init()         // SDL audio device open
  ├── SelfUpdate_init()     // Version check (background)
  ├── ModuleCommon_init()   // Global input state
  ├── Settings_init()       // App settings load
  ├── Resume_init()         // Persisted resume state load
  ├── Downloader_init()     // yt-dlp, queue restore
  │
  └── while (!quit):
        selection = MenuModule_run(screen)
        switch(selection) → run feature module
        handle MODULE_EXIT_QUIT / TG5050 display recovery
  │
  └── cleanup: Background_stopAll, Downloader_cleanup,
               Settings_quit, ModuleCommon_quit,
               SelfUpdate_cleanup, Player_quit, GFX_quit, ...
```

## Module System

Every feature is a self-contained "module" that:
1. Takes `SDL_Surface* screen` as its entry point
2. Owns its own event loop (`SDL_PollEvent` + PAD polling)
3. Returns `ModuleExitReason` (`MODULE_EXIT_TO_MENU` or `MODULE_EXIT_QUIT`)
4. Calls `ModuleCommon_handleGlobalInput()` at the top of each input iteration

```c
// Pattern every module follows:
ModuleExitReason XModule_run(SDL_Surface* screen) {
    // init local state
    while (1) {
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, APP_STATE_X);
        if (global.should_quit) return MODULE_EXIT_QUIT;
        if (global.input_consumed) { dirty = 1; continue; }

        // module-specific input handling
        // rendering when dirty
    }
}
```

### Module Inventory

| Module | Entry point | Purpose |
|--------|------------|---------|
| Menu | `MenuModule_run()` | Main menu (Resume/Now Playing, Library, Radio, Podcast, Settings) |
| Library | `LibraryModule_run()` | File browser → launches PlayerModule |
| Player | `PlayerModule_run()` | Music playback, waveform, spectrum, lyrics |
| Radio | `RadioModule_run()` | Online radio stream playback |
| Podcast | `PodcastModule_run()` | Podcast browse, subscribe, download, play |
| Downloader | (launched from Library/Downloader module) | YouTube Music search + download |
| Settings | `SettingsModule_run()` | App settings (screen timeout, bass, lyrics toggle) |
| System | `SystemModule_run()` | About page, self-update |

## Background Playback

`background.{c,h}` tracks which module (music/radio/podcast) is actively streaming when the user navigates away. The main loop calls `Background_tick()` on each menu iteration for track advancement and resume state saving.

```
BackgroundPlayerType: BG_NONE | BG_MUSIC | BG_RADIO | BG_PODCAST
```

When user selects "Now Playing" from the main menu, `Background_getActive()` routes back to the correct module.

## Global Input (ModuleCommon)

`ModuleCommon_handleGlobalInput()` is the single place that handles:
- **START short-press**: show controls-help overlay
- **START long-press**: show quit-confirmation dialog
- **Volume overlay**: hardware volume buttons + USB HID events
- **Auto screen-off**: idle timeout → backlight off hint → screen off

All modules delegate these to ModuleCommon; they never implement their own quit dialogs.

## Data Persistence

| Data | Location (relative to SD card root) |
|------|--------------------------------------|
| Resume state | `.userdata/shared/music-player/resume.json` |
| App settings | `.userdata/shared/music-player/settings.json` |
| Radio user stations | `.userdata/shared/music-player/radio/stations.txt` |
| Podcast subscriptions | `.userdata/<platform>/music-player/podcast/subscriptions.json` |
| Podcast episodes | `.userdata/<platform>/music-player/podcast/<feed_id>/episodes.json` |
| Podcast progress | `.userdata/<platform>/music-player/podcast/progress.json` |
| YouTube queue | `state/youtube_queue.txt` (pak-local) |
| yt-dlp / wget | `bin/` (pak-local ARM64 binaries) |
| Downloaded music | `Music/Download/` (on SD card) |
| Downloaded podcasts | `Podcasts/<feed>/` (on SD card) |

## Platform Differences

| Feature | tg5040 | tg5050 |
|---------|--------|--------|
| AAC hardware | ALSA only | libfdk-aac.so required |
| Display driver | Standard SDL2/GLES | DRM master; needs DisplayHelper for external processes |
| Audio backend | ALSA | tinyalsa |
| Build flag | `-DPLATFORM="tg5040"` | `-DPLATFORM="tg5050"` + `-ltinyalsa` |

`DisplayHelper_prepareForExternal()` / `_recoverDisplay()` wrap the TG5050-specific DRM release/reacquire needed before launching the on-screen keyboard binary.

## Threading Model

| Thread | Owner | Purpose |
|--------|-------|---------|
| Main thread | `musicplayer.c` | SDL event loop, all rendering |
| Stream thread | `player.c` | Decode + resample PCM into `CircularBuffer` |
| SDL audio callback | SDL internals | Drain `CircularBuffer` → DAC |
| Radio network thread | `radio_net.c` | HTTP receive + ICY metadata |
| Download thread | `downloader.c` | yt-dlp subprocess management |
| Podcast download thread | `podcast.c` | HTTP episode downloads |
| Self-update thread | `selfupdate.c` | GitHub API check + download |
| Podcast search thread | `podcast_search.c` | iTunes API queries |

All cross-thread data goes through `pthread_mutex_t`. Rendering and state queries happen on the main thread only.
