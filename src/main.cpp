#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace cpsbridge {

static bool colorsEqual(ccColor3B a, ccColor3B b, int tolerance = 3) {
    return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) <= tolerance &&
           std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) <= tolerance &&
           std::abs(static_cast<int>(a.b) - static_cast<int>(b.b)) <= tolerance;
}

static bool containsCps(std::string_view text) {
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        auto lower = [](char c) -> char {
            if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
            return c;
        };
        if (lower(text[i]) == 'c' && lower(text[i + 1]) == 'p' &&
            lower(text[i + 2]) == 's') {
            return true;
        }
    }
    return false;
}

static bool megaHackLoaded() {
    return Loader::get()->isModLoaded("absolllute.hackmega") ||
           Loader::get()->isModLoaded("absolllute.megahack");
}

struct TrackedLabel {
    CCLabelBMFont* label = nullptr;
    ccColor3B idleColor{255, 255, 255};
    GLubyte idleOpacity = 255;
    ccColor3B learnedActiveColor{90, 255, 125};
    bool hasLearnedActiveColor = false;
    bool bridgeOwnsPresentation = false;

    void release() {
        if (!label) return;
        if (bridgeOwnsPresentation) {
            label->setColor(idleColor);
            label->setOpacity(idleOpacity);
        }
        label->release();
        label = nullptr;
    }
};

class Bridge final {
public:
    static Bridge& get() {
        static Bridge instance;
        return instance;
    }

    void observeButton(bool pressed, int button, bool player1) {
        if (button != static_cast<int>(PlayerButton::Jump)) return;
        if (player1) m_observedP1 = pressed;
        else m_observedP2 = pressed;
    }

    void drawTick() {
        if (!Mod::get()->getSettingValue<bool>("enabled") || !megaHackLoaded()) {
            resetScene();
            return;
        }

        auto* director = CCDirector::get();
        auto* scene = director ? director->getRunningScene() : nullptr;
        if (!scene) {
            resetScene();
            return;
        }

        if (scene != m_scene) {
            resetLabels();
            m_scene = scene;
            m_lastHeld = false;
        }

        pruneInvalidLabels();
        if (m_labels.empty()) discoverLabels(scene);
        if (m_labels.empty()) return;

        const bool held = currentHeldState();
        const bool rising = held && !m_lastHeld;
        const bool falling = !held && m_lastHeld;

        for (auto& tracked : m_labels) {
            if (!tracked.label) continue;

            auto currentColor = tracked.label->getColor();
            auto currentOpacity = tracked.label->getOpacity();

            if (!held) {
                if (tracked.bridgeOwnsPresentation) {
                    tracked.label->setColor(tracked.idleColor);
                    tracked.label->setOpacity(tracked.idleOpacity);
                    tracked.bridgeOwnsPresentation = false;
                }

                // MegaHack owns the idle style. Keep following it so custom
                // status colours/themes are preserved.
                tracked.idleColor = tracked.label->getColor();
                tracked.idleOpacity = tracked.label->getOpacity();
                continue;
            }

            if (rising) {
                // If MegaHack already changed the colour on this press, this
                // was a path it understood natively. Learn that style and stay
                // out of the way for the whole hold.
                const bool nativeStyleChanged =
                    !colorsEqual(currentColor, tracked.idleColor) ||
                    currentOpacity != tracked.idleOpacity;

                if (nativeStyleChanged) {
                    tracked.learnedActiveColor = currentColor;
                    tracked.hasLearnedActiveColor = true;
                    tracked.bridgeOwnsPresentation = false;
                    continue;
                }

                tracked.bridgeOwnsPresentation = true;
            }

            if (!tracked.bridgeOwnsPresentation) {
                // Continue learning MegaHack's own active style when it is
                // handling the input itself.
                if (!colorsEqual(currentColor, tracked.idleColor)) {
                    tracked.learnedActiveColor = currentColor;
                    tracked.hasLearnedActiveColor = true;
                }
                continue;
            }

            if (falling) {
                tracked.label->setColor(tracked.idleColor);
                tracked.label->setOpacity(tracked.idleOpacity);
                tracked.bridgeOwnsPresentation = false;
                continue;
            }

            const auto active = tracked.hasLearnedActiveColor
                                    ? tracked.learnedActiveColor
                                    : ccColor3B{90, 255, 125};

            float strength = 0.72f;
            if (Mod::get()->getSettingValue<bool>("fallback-pulse")) {
                const auto now = std::chrono::steady_clock::now().time_since_epoch();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                const float phase = static_cast<float>(ms % 520) / 520.0f;
                const float triangle = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
                strength = 0.62f + triangle * 0.22f;
            }

            auto blend = [strength](GLubyte from, GLubyte to) -> GLubyte {
                const float value = static_cast<float>(from) * (1.0f - strength) +
                                    static_cast<float>(to) * strength;
                return static_cast<GLubyte>(std::clamp(value, 0.0f, 255.0f));
            };

            tracked.label->setColor(ccColor3B{
                blend(tracked.idleColor.r, active.r),
                blend(tracked.idleColor.g, active.g),
                blend(tracked.idleColor.b, active.b),
            });
            tracked.label->setOpacity(tracked.idleOpacity);
        }

        m_lastHeld = held;
    }

private:
    CCScene* m_scene = nullptr;
    std::vector<TrackedLabel> m_labels;
    bool m_observedP1 = false;
    bool m_observedP2 = false;
    bool m_lastHeld = false;

    Bridge() = default;
    ~Bridge() { resetLabels(); }

    static bool playerHeld(PlayerObject* player) {
        if (!player) return false;
        // Different bots update slightly different pieces of GD's input state.
        // OR-ing these two canonical fields makes the observer independent of
        // the bot's exact hook path while never generating an input ourselves.
        return player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] ||
               player->m_jumpBuffered;
    }

    bool currentHeldState() {
        if (auto* layer = GJBaseGameLayer::get()) {
            const bool p1 = playerHeld(layer->m_player1);
            const bool p2 = playerHeld(layer->m_player2);
            m_observedP1 = p1;
            m_observedP2 = p2;
        }
        return m_observedP1 || m_observedP2;
    }

    static bool isCandidateLabel(CCLabelBMFont* label) {
        if (!label) return false;

        const auto id = label->getID().view();
        if (id == "id-qt-cps") return true;

        // Do not touch SiliFork's own labels if both mods are installed. The
        // bridge is specifically for MegaHack's status presentation.
        if (id.starts_with("peony.silicate/label.") ||
            id.starts_with("elkiteam.cpsbridge/")) {
            return false;
        }

        const char* raw = label->getString();
        const std::string_view text = raw ? raw : "";
        if (!containsCps(id) && !containsCps(text)) return false;

        // Avoid colouring a separate Max CPS label in fallback discovery.
        auto containsMax = [](std::string_view value) {
            for (size_t i = 0; i + 2 < value.size(); ++i) {
                auto lower = [](char c) -> char {
                    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
                    return c;
                };
                if (lower(value[i]) == 'm' && lower(value[i + 1]) == 'a' &&
                    lower(value[i + 2]) == 'x') return true;
            }
            return false;
        };
        return !containsMax(text);
    }

    void addCandidate(CCLabelBMFont* label) {
        if (!isCandidateLabel(label)) return;
        if (std::any_of(m_labels.begin(), m_labels.end(),
                        [label](const TrackedLabel& item) {
                            return item.label == label;
                        })) {
            return;
        }

        label->retain();
        m_labels.push_back(TrackedLabel{
            .label = label,
            .idleColor = label->getColor(),
            .idleOpacity = label->getOpacity(),
        });
    }

    void scanNode(CCNode* root, int depth) {
        if (!root || depth < 0) return;
        if (auto* label = dynamic_cast<CCLabelBMFont*>(root)) addCandidate(label);

        auto* children = root->getChildren();
        if (!children) return;

        // Avoid descending through giant gameplay object trees. MegaHack
        // statuses are overlay UI and have small ancestry/child counts.
        const auto count = children->count();
        if (count > 192 && depth < 4) return;

        for (unsigned int i = 0; i < count; ++i) {
            auto* child = static_cast<CCNode*>(children->objectAtIndex(i));
            if (!child) continue;
            scanNode(child, depth - 1);
        }
    }

    void discoverLabels(CCScene* scene) {
        if (!scene) return;

        if (auto* layer = PlayLayer::get()) {
            if (layer->m_uiLayer) scanNode(layer->m_uiLayer, 5);
        }

        auto* children = scene->getChildren();
        if (!children) return;
        for (unsigned int i = 0; i < children->count(); ++i) {
            auto* child = static_cast<CCNode*>(children->objectAtIndex(i));
            if (!child) continue;
            if (child == PlayLayer::get()) continue;
            scanNode(child, 5);
        }
    }

    void pruneInvalidLabels() {
        for (auto it = m_labels.begin(); it != m_labels.end();) {
            if (!it->label || !it->label->getParent()) {
                it->release();
                it = m_labels.erase(it);
            } else {
                ++it;
            }
        }
    }

    void resetLabels() {
        for (auto& tracked : m_labels) tracked.release();
        m_labels.clear();
    }

    void resetScene() {
        resetLabels();
        m_scene = nullptr;
        m_observedP1 = false;
        m_observedP2 = false;
        m_lastHeld = false;
    }
};

} // namespace cpsbridge

struct CPSBridgeGameLayer : Modify<CPSBridgeGameLayer, GJBaseGameLayer> {
    void handleButton(bool pressed, int button, bool player1) {
        cpsbridge::Bridge::get().observeButton(pressed, button, player1);
        GJBaseGameLayer::handleButton(pressed, button, player1);
    }
};

struct CPSBridgeDirector : Modify<CPSBridgeDirector, CCDirector> {
    void drawScene() {
        cpsbridge::Bridge::get().drawTick();
        CCDirector::drawScene();
    }
};
