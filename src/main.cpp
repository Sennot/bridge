#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace cpsbridge {

struct LabelState {
    cocos2d::CCLabelBMFont* label = nullptr;
    cocos2d::ccColor3B color{};
    GLubyte opacity = 255;
};

static bool containsCpsToken(std::string_view text) {
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        const char a = static_cast<char>(text[i] | 0x20);
        const char b = static_cast<char>(text[i + 1] | 0x20);
        const char c = static_cast<char>(text[i + 2] | 0x20);
        if (a == 'c' && b == 'p' && c == 's') return true;
    }
    return false;
}

class Bridge final {
public:
    static Bridge& get() {
        static Bridge instance;
        return instance;
    }

    void reloadSettings() {
        m_enabled = Mod::get()->getSettingValue<bool>("enabled");
        m_allButtons = Mod::get()->getSettingValue<bool>("all-buttons");
        m_fallbackScan = Mod::get()->getSettingValue<bool>("fallback-cps-scan");
        m_debugLog = Mod::get()->getSettingValue<bool>("debug-log");

        if (!m_enabled) {
            clearHeld();
            restoreAndRelease();
        }
    }

    void onInput(bool down, int button, bool isPlayer1) {
        if (!m_enabled) return;
        if (button < 1 || button > 3) return;
        if (!m_allButtons && button != 1) return;

        const size_t player = isPlayer1 ? 0u : 1u;
        m_held[player][static_cast<size_t>(button)] = down;

        if (m_debugLog) {
            geode::log::debug(
                "MegaHack CPS Bridge input: P{} button={} {}",
                isPlayer1 ? 1 : 2,
                button,
                down ? "press" : "release"
            );
        }
    }

    void clearHeld() {
        for (auto& player : m_held) player.fill(false);
    }

    void present() {
        auto* scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
        if (scene != m_scene) {
            restoreAndRelease();
            m_scene = scene;
        }

        if (!m_enabled || !scene || !isMegaHackLoaded()) {
            if (!m_labels.empty()) restoreAndRelease();
            return;
        }

        const bool active = anyHeld();
        if (!active) {
            // Learn MegaHack's current idle/theme colour while we are not
            // presenting an automated/manual held state. This prevents the
            // bridge from permanently overwriting user theme changes.
            if (!m_labels.empty()) {
                for (auto& state : m_labels) {
                    if (!state.label) continue;
                    state.label->setColor(state.color);
                    state.label->setOpacity(state.opacity);
                }
                restoreAndRelease();
            }
            return;
        }

        if (m_labels.empty()) discover(scene);
        if (m_labels.empty()) return;

        // Exact SiliFork presentation style: green triangle pulse while an
        // input is held. This is presentation-only; no gameplay input is
        // synthesized and MegaHack's CPS numeric counting is untouched.
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        const int wave = static_cast<int>(ms % 520);
        const int triangle = wave <= 260 ? wave : 520 - wave;
        const int pulse = 185 + triangle * 70 / 260;
        const cocos2d::ccColor3B target{90, 255, 125};

        const auto blend = [pulse](GLubyte from, GLubyte to) -> GLubyte {
            return static_cast<GLubyte>(
                (static_cast<int>(from) * (255 - pulse) +
                 static_cast<int>(to) * pulse) /
                255
            );
        };

        for (auto& state : m_labels) {
            if (!state.label) continue;
            state.label->setColor(cocos2d::ccColor3B{
                blend(state.color.r, target.r),
                blend(state.color.g, target.g),
                blend(state.color.b, target.b)
            });
            state.label->setOpacity(state.opacity);
        }
    }

private:
    bool m_enabled = true;
    bool m_allButtons = false;
    bool m_fallbackScan = true;
    bool m_debugLog = false;
    std::array<std::array<bool, 4>, 2> m_held{};
    cocos2d::CCScene* m_scene = nullptr;
    std::vector<LabelState> m_labels;

    static bool isMegaHackLoaded() {
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
    }

    bool anyHeld() const {
        for (const auto& player : m_held) {
            const size_t lastButton = m_allButtons ? 3u : 1u;
            for (size_t button = 1; button <= lastButton; ++button) {
                if (player[button]) return true;
            }
        }
        return false;
    }

    static bool isExcludedLabel(cocos2d::CCLabelBMFont* label) {
        if (!label) return true;
        const auto id = label->getID().view();
        return id.starts_with("peony.silicate/label.") ||
               id.starts_with(Mod::get()->getID() + "/");
    }

    static bool isCpsLabel(cocos2d::CCLabelBMFont* label) {
        if (!label || isExcludedLabel(label)) return false;
        const char* raw = label->getString();
        const std::string_view text = raw ? raw : "";
        return containsCpsToken(text) || containsCpsToken(label->getID().view());
    }

    static bool hasMegaHackHint(cocos2d::CCNode* node) {
        int depth = 0;
        while (node && depth++ < 6) {
            const auto id = node->getID().view();
            if (id.find("hackmega") != std::string_view::npos ||
                id.find("megahack") != std::string_view::npos ||
                id.find("absolllute") != std::string_view::npos) {
                return true;
            }
            node = node->getParent();
        }
        return false;
    }

    static void collectLabels(
        cocos2d::CCNode* node,
        int depth,
        int& budget,
        std::vector<cocos2d::CCLabelBMFont*>& strict,
        std::vector<cocos2d::CCLabelBMFont*>& fallback
    ) {
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
            collectLabels(child, depth + 1, budget, strict, fallback);
        }
    }

    void discover(cocos2d::CCScene* scene) {
        std::vector<cocos2d::CCLabelBMFont*> strict;
        std::vector<cocos2d::CCLabelBMFont*> fallback;
        int budget = 1800;

        collectLabels(scene, 0, budget, strict, fallback);
        if (auto* playLayer = PlayLayer::get()) {
            collectLabels(playLayer, 0, budget, strict, fallback);
            collectLabels(playLayer->m_uiLayer, 0, budget, strict, fallback);
        }

        const auto& selected = !strict.empty() ? strict : fallback;
        if (strict.empty() && !m_fallbackScan) return;

        for (auto* label : selected) {
            if (!label) continue;
            if (std::find_if(m_labels.begin(), m_labels.end(), [label](const LabelState& s) {
                    return s.label == label;
                }) != m_labels.end()) {
                continue;
            }

            label->retain();
            m_labels.push_back(LabelState{
                label,
                label->getColor(),
                label->getOpacity()
            });
        }

        if (m_debugLog) {
            geode::log::info("MegaHack CPS Bridge: found {} CPS label(s)", m_labels.size());
        }
    }

    void restoreAndRelease() {
        for (auto& state : m_labels) {
            if (!state.label) continue;
            state.label->setColor(state.color);
            state.label->setOpacity(state.opacity);
            state.label->release();
        }
        m_labels.clear();
    }
};

} // namespace cpsbridge

struct $modify(CpsBridgeGJBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // Observe the final GD input path. We intentionally do not try to
        // identify individual bot DLLs: any bot that feeds real gameplay via
        // GJBaseGameLayer::handleButton automatically gets compatibility.
        if (!self.setHookPriorityPre("GJBaseGameLayer::handleButton", Priority::First)) {
            geode::log::warn("MegaHack CPS Bridge: failed to set handleButton priority");
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        cpsbridge::Bridge::get().onInput(down, button, isPlayer1);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

struct $modify(CpsBridgeDirector, cocos2d::CCDirector) {
    void drawScene() {
        // SiliFork applies its CPS presentation immediately before rendering;
        // do the same so MegaHack has already updated its own status text.
        cpsbridge::Bridge::get().present();
        cocos2d::CCDirector::drawScene();
    }
};

struct $modify(CpsBridgePlayLayer, PlayLayer) {
    void resetLevel() {
        cpsbridge::Bridge::get().clearHeld();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        cpsbridge::Bridge::get().clearHeld();
        PlayLayer::onQuit();
    }
};

$execute {
    cpsbridge::Bridge::get().reloadSettings();

    geode::listenForSettingChanges<bool>("enabled", +[](bool) {
        cpsbridge::Bridge::get().reloadSettings();
    });
    geode::listenForSettingChanges<bool>("all-buttons", +[](bool) {
        cpsbridge::Bridge::get().reloadSettings();
    });
    geode::listenForSettingChanges<bool>("fallback-cps-scan", +[](bool) {
        cpsbridge::Bridge::get().reloadSettings();
    });
    geode::listenForSettingChanges<bool>("debug-log", +[](bool) {
        cpsbridge::Bridge::get().reloadSettings();
    });
}
