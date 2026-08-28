#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#include <intrin.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace botcps {

using Clock = std::chrono::steady_clock;

static std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static bool containsInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;

    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool matches = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            const auto a = static_cast<unsigned char>(haystack[i + j]);
            const auto b = static_cast<unsigned char>(needle[j]);
            if (std::tolower(a) != std::tolower(b)) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

static bool containsCpsToken(std::string_view text) {
    return containsInsensitive(text, "cps");
}

struct Config {
    bool enabled = true;
    int pulsePeriodMs = 520;
    int pulseStrength = 100;
    int pulseLingerMs = 110;
    bool allButtons = false;
    bool genericDetection = true;
    bool fallbackCpsScan = true;
    bool debugLog = false;
    std::vector<std::string> extraModuleTokens;
};

struct LabelState {
    cocos2d::CCLabelBMFont* label = nullptr;
    cocos2d::ccColor3B baseColor{};
    GLubyte baseOpacity = 255;
};

class Indicator final {
public:
    static Indicator& get() {
        static Indicator instance;
        return instance;
    }

    void reloadConfig() {
        auto* mod = Mod::get();
        m_config.enabled = mod->getSettingValue<bool>("enabled");
        m_config.pulsePeriodMs = std::clamp(mod->getSettingValue<int>("pulse-period"), 160, 1400);
        m_config.pulseStrength = std::clamp(mod->getSettingValue<int>("pulse-strength"), 20, 100);
        m_config.pulseLingerMs = std::clamp(mod->getSettingValue<int>("pulse-linger"), 0, 500);
        m_config.allButtons = mod->getSettingValue<bool>("all-buttons");
        m_config.genericDetection = mod->getSettingValue<bool>("generic-detection");
        m_config.fallbackCpsScan = mod->getSettingValue<bool>("fallback-cps-scan");
        m_config.debugLog = mod->getSettingValue<bool>("debug-log");

        m_config.extraModuleTokens.clear();
        auto raw = lowerAscii(mod->getSettingValue<std::string>("extra-module-tokens"));
        std::string current;
        for (char c : raw) {
            if (c == ',' || c == ';') {
                pushExtraToken(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        pushExtraToken(current);

        if (!m_config.enabled) {
            clearInputState();
            restoreLabels();
        }
    }

    std::optional<std::string> classifyCaller(void* returnAddress) const {
#ifdef GEODE_IS_WINDOWS
        if (!m_config.enabled || !returnAddress) return std::nullopt;

        HMODULE module = nullptr;
        const auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (!GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(returnAddress), &module) || !module) {
            return std::nullopt;
        }

        std::array<char, 1024> path{};
        const auto length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return std::nullopt;

        std::string moduleName(path.data(), length);
        const auto slash = moduleName.find_last_of("\\/");
        if (slash != std::string::npos) moduleName.erase(0, slash + 1);
        moduleName = lowerAscii(std::move(moduleName));

        // Never classify the host, Geode itself, or this observer as bot input.
        static constexpr std::array<std::string_view, 5> excluded = {
            "geometrydash.exe",
            "geode.dll",
            "elkiteam.botcpsglow",
            "bot_cps_glow",
            "botcpsglow"
        };
        for (auto token : excluded) {
            if (moduleName.find(token) != std::string::npos) return std::nullopt;
        }

        struct KnownBot {
            std::string_view token;
            std::string_view display;
        };
        static constexpr std::array<KnownBot, 17> known = {{
            {"peony.silicate", "SiliFork"},
            {"silifork", "SiliFork"},
            {"zilko.xdbot", "xdBot"},
            {"xdbot", "xdBot"},
            {"chagh.tcbot", "tcBot"},
            {"tcbot", "tcBot"},
            {"fig.zbot", "zBot"},
            {"zbot", "zBot"},
            {"kepe.ybot", "yBot"},
            {"ybot", "yBot"},
            {"astralteam.astral", "Astral"},
            {"matcool.replaybot", "ReplayBot"},
            {"replaybot", "ReplayBot"},
            {"absolllute.hackmega", "Mega Hack"},
            {"absolllute.megahack", "Mega Hack"},
            {"hackmega", "Mega Hack"},
            {"megahack", "Mega Hack"}
        }};

        for (const auto& entry : known) {
            if (moduleName.find(entry.token) != std::string::npos) {
                return std::string(entry.display);
            }
        }

        for (const auto& token : m_config.extraModuleTokens) {
            if (!token.empty() && moduleName.find(token) != std::string::npos) {
                return moduleName;
            }
        }

        if (m_config.genericDetection) {
            static constexpr std::array<std::string_view, 7> generic = {
                "bot", "macro", "replay", "tas", "autoclick", "clickbot", "autoplay"
            };
            for (auto token : generic) {
                if (moduleName.find(token) != std::string::npos) {
                    return moduleName;
                }
            }
        }
#else
        (void)returnAddress;
#endif
        return std::nullopt;
    }

    void onBotInput(bool hold, int button, bool player2, std::string source) {
        if (!m_config.enabled) return;
        if (button < 1 || button > 3) return;
        if (!m_config.allButtons && button != 1) return;

        const size_t player = player2 ? 1u : 0u;
        m_held[player][static_cast<size_t>(button)] = hold;
        m_lastBotEvent = Clock::now();

        if (m_config.debugLog && source != m_lastLoggedSource) {
            geode::log::info("Bot CPS Glow: automated input source -> {}", source);
            m_lastLoggedSource = std::move(source);
        }
    }

    void clearInputState() {
        for (auto& player : m_held) player.fill(false);
        m_lastBotEvent = Clock::time_point{};
        m_lastLoggedSource.clear();
    }

    void updateVisual() {
        auto* scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
        if (scene != m_scene) {
            releaseLabels(true);
            m_scene = scene;
            m_nextScan = Clock::time_point{};
            m_wasActive = false;
        }

        if (!m_config.enabled || !isMegaHackLoaded() || !scene) {
            if (m_wasActive) restoreLabels();
            m_wasActive = false;
            return;
        }

        const auto now = Clock::now();
        if (m_labels.empty() && now >= m_nextScan) {
            discoverLabels(scene);
            m_nextScan = now + std::chrono::milliseconds(350);
        }

        if (m_labels.empty()) return;

        const bool active = pulseActive(now);
        if (!active) {
            if (m_wasActive) {
                restoreLabels();
            } else {
                // While idle, learn the current Mega Hack colour so theme/status
                // changes are preserved the next time the green pulse starts.
                for (auto& state : m_labels) {
                    if (!state.label) continue;
                    state.baseColor = state.label->getColor();
                    state.baseOpacity = state.label->getOpacity();
                }
            }
            m_wasActive = false;
            return;
        }

        if (!m_wasActive) {
            for (auto& state : m_labels) {
                if (!state.label) continue;
                state.baseColor = state.label->getColor();
                state.baseOpacity = state.label->getOpacity();
            }
        }

        applyPulse(now);
        m_wasActive = true;
    }

private:
    Config m_config;
    std::array<std::array<bool, 4>, 2> m_held{};
    Clock::time_point m_lastBotEvent{};
    Clock::time_point m_nextScan{};
    cocos2d::CCScene* m_scene = nullptr;
    std::vector<LabelState> m_labels;
    bool m_wasActive = false;
    std::string m_lastLoggedSource;

    Indicator() = default;
    ~Indicator() = default;

    void pushExtraToken(std::string token) {
        auto first = token.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return;
        auto last = token.find_last_not_of(" \t\r\n");
        token = token.substr(first, last - first + 1);
        if (!token.empty()) m_config.extraModuleTokens.push_back(std::move(token));
    }

    static bool isMegaHackLoaded() {
        static const bool loaded = [] {
            auto* loader = Loader::get();
            if (loader->isModLoaded("absolllute.hackmega") ||
                loader->isModLoaded("absolllute.megahack")) {
                return true;
            }
#ifdef GEODE_IS_WINDOWS
            return GetModuleHandleA("absolllute.hackmega.dll") != nullptr ||
                   GetModuleHandleA("absolllute.megahack.dll") != nullptr;
#else
            return false;
#endif
        }();
        return loaded;
    }

    bool pulseActive(Clock::time_point now) const {
        for (const auto& player : m_held) {
            for (size_t button = 1; button <= 3; ++button) {
                if (player[button]) return true;
            }
        }

        if (m_lastBotEvent == Clock::time_point{} || m_config.pulseLingerMs <= 0) {
            return false;
        }
        return now - m_lastBotEvent <= std::chrono::milliseconds(m_config.pulseLingerMs);
    }

    static bool isExcludedCpsLabel(cocos2d::CCLabelBMFont* label) {
        if (!label) return true;
        const auto id = label->getID().view();
        return id.starts_with("peony.silicate/label.") ||
               id.starts_with("elkiteam.botcpsglow/");
    }

    static bool isCpsLabel(cocos2d::CCLabelBMFont* label) {
        if (!label || isExcludedCpsLabel(label)) return false;
        const char* raw = label->getString();
        const std::string_view text = raw ? raw : "";
        return containsCpsToken(text) || containsCpsToken(label->getID().view());
    }

    static bool hasMegaHackHint(cocos2d::CCNode* node) {
        int depth = 0;
        while (node && depth++ < 6) {
            const auto id = node->getID().view();
            if (containsInsensitive(id, "hackmega") ||
                containsInsensitive(id, "megahack") ||
                containsInsensitive(id, "absolllute")) {
                return true;
            }
            node = node->getParent();
        }
        return false;
    }

    static void collectCpsLabels(cocos2d::CCNode* node,
                                 int depth,
                                 int& budget,
                                 std::vector<cocos2d::CCLabelBMFont*>& strict,
                                 std::vector<cocos2d::CCLabelBMFont*>& fallback) {
        if (!node || budget-- <= 0) return;

        if (auto* label = dynamic_cast<cocos2d::CCLabelBMFont*>(node); isCpsLabel(label)) {
            if (hasMegaHackHint(label)) strict.push_back(label);
            else fallback.push_back(label);
        }

        if (depth >= 6) return;
        auto* children = node->getChildren();
        if (!children) return;

        for (unsigned int i = 0; i < children->count() && budget > 0; ++i) {
            auto* child = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
            collectCpsLabels(child, depth + 1, budget, strict, fallback);
        }
    }

    void discoverLabels(cocos2d::CCNode* root) {
        std::vector<cocos2d::CCLabelBMFont*> strict;
        std::vector<cocos2d::CCLabelBMFont*> fallback;
        int budget = 1800;
        collectCpsLabels(root, 0, budget, strict, fallback);

        const auto& selected = !strict.empty() ? strict : fallback;
        if (strict.empty() && !m_config.fallbackCpsScan) return;

        for (auto* label : selected) {
            if (!label) continue;
            const auto duplicate = std::find_if(m_labels.begin(), m_labels.end(),
                [label](const LabelState& state) { return state.label == label; });
            if (duplicate != m_labels.end()) continue;

            label->retain();
            m_labels.push_back(LabelState{label, label->getColor(), label->getOpacity()});
        }

        if (m_config.debugLog && !m_labels.empty()) {
            geode::log::info("Bot CPS Glow: found {} CPS label(s)", m_labels.size());
        }
    }

    void applyPulse(Clock::time_point now) {
        const int period = std::max(160, m_config.pulsePeriodMs);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        const int phase = static_cast<int>(ms % period);
        const float half = static_cast<float>(period) * 0.5f;
        const float triangle = phase <= half ? phase / half : (period - phase) / half;
        const float strength = static_cast<float>(m_config.pulseStrength) / 100.0f;
        const float blendAmount = std::clamp((0.48f + 0.52f * triangle) * strength, 0.0f, 1.0f);
        const cocos2d::ccColor3B target{90, 255, 125};

        const auto blend = [blendAmount](GLubyte from, GLubyte to) -> GLubyte {
            const float value = static_cast<float>(from) +
                                (static_cast<float>(to) - static_cast<float>(from)) * blendAmount;
            return static_cast<GLubyte>(std::clamp(value, 0.0f, 255.0f));
        };

        for (auto& state : m_labels) {
            if (!state.label) continue;
            state.label->setColor(cocos2d::ccColor3B{
                blend(state.baseColor.r, target.r),
                blend(state.baseColor.g, target.g),
                blend(state.baseColor.b, target.b)
            });
            state.label->setOpacity(state.baseOpacity);
        }
    }

    void restoreLabels() {
        for (auto& state : m_labels) {
            if (!state.label) continue;
            state.label->setColor(state.baseColor);
            state.label->setOpacity(state.baseOpacity);
        }
    }

    void releaseLabels(bool restore) {
        for (auto& state : m_labels) {
            if (!state.label) continue;
            if (restore) {
                state.label->setColor(state.baseColor);
                state.label->setOpacity(state.baseOpacity);
            }
            state.label->release();
        }
        m_labels.clear();
    }
};

} // namespace botcps

struct $modify(BotCpsGJBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // Keep this observer outermost. For normal human input the immediate
        // caller remains GeometryDash.exe; bot-injected calls identify the bot
        // DLL itself. This also avoids treating an inner bot hook that merely
        // observes manual input as automation.
        if (!self.setHookPriorityPre("GJBaseGameLayer::handleButton", Priority::First)) {
            geode::log::warn("Bot CPS Glow: failed to set outer handleButton priority");
        }
    }

    void handleButton(bool hold, int button, bool player2) {
        void* caller = nullptr;
#ifdef GEODE_IS_WINDOWS
        caller = _ReturnAddress();
#endif
        auto source = botcps::Indicator::get().classifyCaller(caller);

        GJBaseGameLayer::handleButton(hold, button, player2);

        if (source) {
            botcps::Indicator::get().onBotInput(hold, button, player2, std::move(*source));
        }
    }
};

struct $modify(BotCpsScheduler, cocos2d::CCScheduler) {
    static void onModify(auto& self) {
        // Run outermost and apply the colour after all inner scheduler hooks,
        // including Mega Hack's own status-label updates.
        if (!self.setHookPriorityPre("cocos2d::CCScheduler::update", Priority::First)) {
            geode::log::warn("Bot CPS Glow: failed to set scheduler priority");
        }
    }

    void update(float dt) {
        cocos2d::CCScheduler::update(dt);
        botcps::Indicator::get().updateVisual();
    }
};

struct $modify(BotCpsPlayLayer, PlayLayer) {
    void resetLevel() {
        botcps::Indicator::get().clearInputState();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        botcps::Indicator::get().clearInputState();
        PlayLayer::onQuit();
    }
};

$execute {
    botcps::Indicator::get().reloadConfig();

    geode::listenForSettingChanges<bool>("enabled", +[](bool) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<int>("pulse-period", +[](int) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<int>("pulse-strength", +[](int) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<int>("pulse-linger", +[](int) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<bool>("all-buttons", +[](bool) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<bool>("generic-detection", +[](bool) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<std::string>("extra-module-tokens", +[](std::string) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<bool>("fallback-cps-scan", +[](bool) {
        botcps::Indicator::get().reloadConfig();
    });
    geode::listenForSettingChanges<bool>("debug-log", +[](bool) {
        botcps::Indicator::get().reloadConfig();
    });
}
