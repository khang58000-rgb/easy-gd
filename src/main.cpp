// Instant Jump - Geode mod for Geometry Dash 2.2081
// Developer: KYLZNX  |  id: khang58033.instant-jump
//
// What it does
//  * Instant click: a press/release is handed to the game the moment the click
//    event arrives, instead of waiting in the game's own queue for the next frame.
//  * Input queuing ("step guard"): at most one press/release is applied per
//    physics step; extra ones are queued so every tap lasts a full step.
//    This keeps ship/UFO taps and Wave spam from collapsing into nothing.
//  * Double-click fix: presses closer together than a human can click are dropped.
//  * Input buffering: a click made shortly before landing fires on landing.
//  * Slope assist: bigger buffer window while on / just off a slope.
//  * Jump prediction: buffer window grows with fall speed.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>

using namespace geode::prelude;

namespace instantjump {

    // ------------------------------------------------------------------
    // Time
    // ------------------------------------------------------------------
    static double nowMs() {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }

    // ------------------------------------------------------------------
    // Settings (cached for 250 ms so we never hammer the settings map)
    // ------------------------------------------------------------------
    struct Config {
        bool enabled = true;
        bool stepGuard = true;
        bool doubleClickFix = true;
        bool buffering = true;
        bool slopeAssist = true;
        bool prediction = true;
        int doubleClickMs = 8;
        int bufferMs = 70;
        int queueSize = 2;
        int slopeExtraMs = 25;
        int predictionMs = 30;
    };

    static Config const& getConfig() {
        static Config cfg;
        static double lastLoad = -1e9;
        double const now = nowMs();
        if (now - lastLoad > 250.0) {
            lastLoad = now;
            auto* m = Mod::get();
            cfg.enabled = m->getSettingValue<bool>("enabled");
            cfg.stepGuard = m->getSettingValue<bool>("step-guard");
            cfg.doubleClickFix = m->getSettingValue<bool>("double-click-fix");
            cfg.buffering = m->getSettingValue<bool>("buffering");
            cfg.slopeAssist = m->getSettingValue<bool>("slope-assist");
            cfg.prediction = m->getSettingValue<bool>("prediction");
            cfg.doubleClickMs = static_cast<int>(m->getSettingValue<int64_t>("double-click-ms"));
            cfg.bufferMs = static_cast<int>(m->getSettingValue<int64_t>("buffer-ms"));
            cfg.queueSize = static_cast<int>(m->getSettingValue<int64_t>("queue-size"));
            cfg.slopeExtraMs = static_cast<int>(m->getSettingValue<int64_t>("slope-extra-ms"));
            cfg.predictionMs = static_cast<int>(m->getSettingValue<int64_t>("prediction-ms"));
        }
        return cfg;
    }

    // ------------------------------------------------------------------
    // Per-player input state. Index 0 = player 1 input, 1 = player 2 input.
    // ------------------------------------------------------------------
    constexpr size_t kMaxQueuedActions = 8;

    struct Slot {
        bool holding = false;          // what the user's finger/key is doing
        bool changedThisStep = false;  // we already changed the game's button state this step
        double lastPushMs = -1e9;      // for the double-click fix
        double lastSlopeMs = -1e9;     // last time this player was on a slope
        std::deque<bool> actions;      // queued button changes (true = press, false = release)
        std::deque<double> buffered;   // timestamps of clicks made while airborne

        void reset() {
            holding = false;
            changedThisStep = false;
            lastPushMs = -1e9;
            lastSlopeMs = -1e9;
            actions.clear();
            buffered.clear();
        }
    };

    static Slot g_slots[2];

    // Gamemodes that can only act while touching ground (cube, robot, spider).
    // Ship, UFO, ball, wave and swing react in mid-air, so they are never buffered.
    static bool isGroundMode(PlayerObject* p) {
        return !(p->m_isShip || p->m_isBird || p->m_isBall || p->m_isDart || p->m_isSwing);
    }

    // Applies a press/release now, or queues it if this step already had a change.
    static void submit(GJBaseGameLayer* layer, Slot& s, bool down, bool isPlayer1, Config const& cfg) {
        bool const canApplyNow = !cfg.stepGuard || (!s.changedThisStep && s.actions.empty());
        if (canApplyNow) {
            layer->handleButton(down, 1, isPlayer1);
            s.changedThisStep = true;
        }
        else {
            if (s.actions.size() >= kMaxQueuedActions) {
                // Overflow: drop the backlog but keep the newest state so we never get stuck held.
                s.actions.clear();
            }
            s.actions.push_back(down);
        }
    }

}

using namespace instantjump;

// ----------------------------------------------------------------------
// Click entry point: this is where touches / key presses arrive.
// We take over button 1 (jump / fly) inside PlayLayer only.
// ----------------------------------------------------------------------
class $modify(InstantJumpGameLayer, GJBaseGameLayer) {
    void queueButton(int button, bool down, bool isPlayer1) {
        auto* pl = PlayLayer::get();
        if (!pl || button != 1 ||
            static_cast<GJBaseGameLayer*>(this) != static_cast<GJBaseGameLayer*>(pl)) {
            GJBaseGameLayer::queueButton(button, down, isPlayer1);
            return;
        }

        auto const& cfg = getConfig();
        if (!cfg.enabled) {
            GJBaseGameLayer::queueButton(button, down, isPlayer1);
            return;
        }

        auto& s = g_slots[isPlayer1 ? 0 : 1];
        double const now = nowMs();

        if (down) {
            // Double-click fix: swallow impossible-speed repeat presses.
            if (cfg.doubleClickFix && (now - s.lastPushMs) < static_cast<double>(cfg.doubleClickMs)) {
                return;
            }
            s.lastPushMs = now;
            s.holding = true;

            // Input buffering: remember clicks made while we cannot act yet.
            if (cfg.buffering && cfg.bufferMs > 0) {
                PlayerObject* p = isPlayer1 ? pl->m_player1 : pl->m_player2;
                if (p && isGroundMode(p) && !p->m_isOnGround && !p->m_isOnSlope) {
                    if (static_cast<int>(s.buffered.size()) >= cfg.queueSize) {
                        s.buffered.pop_front();
                    }
                    s.buffered.push_back(now);
                }
            }
        }
        else {
            s.holding = false;
        }

        // Instant click: do not wait for the game's own queue.
        submit(this, s, down, isPlayer1, cfg);
    }
};

// ----------------------------------------------------------------------
// Runs once per physics step for each player. Here we
//  1. release the "changed this step" lock,
//  2. fire buffered clicks on landing (with slope + prediction widening),
//  3. apply the next queued press/release.
// ----------------------------------------------------------------------
class $modify(InstantJumpPlayer, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);

        auto* pl = PlayLayer::get();
        if (!pl) return;

        int idx;
        if (static_cast<PlayerObject*>(this) == pl->m_player1) idx = 0;
        else if (static_cast<PlayerObject*>(this) == pl->m_player2) idx = 1;
        else return;

        auto const& cfg = getConfig();
        if (!cfg.enabled) return;

        auto& s = g_slots[idx];
        s.changedThisStep = false;

        double const now = nowMs();
        if (this->m_isOnSlope) s.lastSlopeMs = now;

        // --- buffered clicks -> fire on landing --------------------------
        if (!s.buffered.empty()) {
            bool const grounded = this->m_isOnGround || this->m_isOnSlope;

            double window = static_cast<double>(cfg.bufferMs);

            // Slope assist: ground flag flickers on slopes, so be more forgiving.
            if (cfg.slopeAssist && (now - s.lastSlopeMs) < 120.0) {
                window += static_cast<double>(cfg.slopeExtraMs);
            }

            // Jump prediction: faster fall -> more ground covered per ms -> wider window.
            if (cfg.prediction) {
                double const v = this->m_yVelocity * (this->m_isUpsideDown ? -1.0 : 1.0);
                if (v < 0.0) {
                    window += static_cast<double>(cfg.predictionMs) * std::min(1.0, -v / 16.0);
                }
            }

            while (!s.buffered.empty() && (now - s.buffered.front()) > window) {
                s.buffered.pop_front();
            }

            if (grounded && !s.buffered.empty() && s.actions.empty()) {
                s.buffered.pop_front();
                // If the finger is still down the game re-jumps by itself on landing.
                // Otherwise inject a tap: press now, release next step.
                if (!s.holding) {
                    s.actions.push_back(true);
                    s.actions.push_back(false);
                }
            }
        }

        // --- apply next queued change -------------------------------------
        if (!s.actions.empty()) {
            bool const down = s.actions.front();
            s.actions.pop_front();
            pl->handleButton(down, 1, idx == 0);
            s.changedThisStep = true;
        }
    }
};

// ----------------------------------------------------------------------
// Clear all state on (re)spawn so nothing leaks between attempts.
// ----------------------------------------------------------------------
class $modify(InstantJumpPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        for (auto& s : g_slots) s.reset();
    }
};

$on_mod(Loaded) {
    log::info("Instant Jump loaded");
}
