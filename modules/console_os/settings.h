#pragma once
#include <stdint.h>
#include <string.h>

// Persisted console state: the three knobs the user can turn that must survive a
// reboot. themeIndex indexes ThemeManager; volume feeds Audio::setVolume(v/255);
// brightness attenuates the active theme's LightProfile.wallBrightness ceiling.
// SettingsStore is the storage seam: NVS on hardware, file/in-memory on the host.

namespace console_os {

struct Settings {
    uint8_t themeIndex = 0;
    uint8_t volume     = 40;   // 0..255 -> Audio::setVolume(v / 255.0f); quiet by default
    uint8_t brightness = 255;  // 0..255 -> scales theme wallBrightness ceiling
};

// Per-detent step for the settings sliders (~16 detents spans the full range).
constexpr int VOL_STEP    = 16;
constexpr int BRIGHT_STEP = 16;

class SettingsStore {
public:
    virtual ~SettingsStore() {}
    // Fill out with stored values; return false if nothing has been saved yet.
    virtual bool load(Settings& out) = 0;
    virtual void save(const Settings& s) = 0;
};

// Host tests / boards with no persistence: keeps the last save in RAM.
class MemorySettingsStore : public SettingsStore {
public:
    bool load(Settings& out) override {
        if (!has_) return false;
        out = s_;
        return true;
    }
    void save(const Settings& s) override {
        s_ = s;
        has_ = true;
        saves_++;
    }
    int saveCount() const { return saves_; }

private:
    Settings s_;
    bool     has_ = false;
    int      saves_ = 0;
};

#ifdef CONSOLE_OS_HOST
// Desktop persistence stub: a 6-byte little file next to the binary. Proves the
// load/apply-on-boot path end to end without touching NVS.
}  // namespace console_os
#include <stdio.h>
namespace console_os {

class FileSettingsStore : public SettingsStore {
public:
    explicit FileSettingsStore(const char* path) : path_(path) {}

    bool load(Settings& out) override {
        FILE* f = fopen(path_, "rb");
        if (!f) return false;
        uint8_t b[6] = {0};
        size_t n = fread(b, 1, sizeof(b), f);
        fclose(f);
        if (n != sizeof(b) || b[0] != 'S' || b[1] != 'O' || b[2] != '1') return false;
        out.themeIndex = b[3];
        out.volume     = b[4];
        out.brightness = b[5];
        return true;
    }
    void save(const Settings& s) override {
        FILE* f = fopen(path_, "wb");
        if (!f) return;
        uint8_t b[6] = {'S', 'O', '1', s.themeIndex, s.volume, s.brightness};
        fwrite(b, 1, sizeof(b), f);
        fclose(f);
    }

private:
    const char* path_;
};
#endif  // CONSOLE_OS_HOST

#ifdef CONSOLE_OS_NVS
// On-device persistence: ESP32 NVS via the Arduino Preferences library. Only
// compiled into the firmware build (host tests never pull in Preferences.h).
}  // namespace console_os
#include <Preferences.h>
namespace console_os {

class NvsSettingsStore : public SettingsStore {
public:
    bool load(Settings& out) override {
        Preferences p;
        if (!p.begin(NS, /*readOnly=*/true)) return false;
        bool has = p.isKey("theme");
        out.themeIndex = p.getUChar("theme", out.themeIndex);
        out.volume     = p.getUChar("vol", out.volume);
        out.brightness = p.getUChar("bright", out.brightness);
        p.end();
        return has;
    }
    void save(const Settings& s) override {
        Preferences p;
        if (!p.begin(NS, /*readOnly=*/false)) return;
        p.putUChar("theme", s.themeIndex);
        p.putUChar("vol", s.volume);
        p.putUChar("bright", s.brightness);
        p.end();
    }

private:
    static constexpr const char* NS = "brainos";
};
#endif  // CONSOLE_OS_NVS

}  // namespace console_os
