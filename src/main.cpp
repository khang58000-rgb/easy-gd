// Instant Jump - Geode mod for Geometry Dash 2.2081
// Developer: KYLZNX  |  id: khang58033.instant-jump
//
// GD 2.2081 already stamps every click with a timestamp and applies it at the
// right sub-step. This mod never delays or reorders that. It hooks
// GJBaseGameLayer::handleButton (where the game applies a press/release) and adds:
//  * Double-click fix: presses closer together than a human can click are dropped.
//  * Input buffering / queue: a click made shortly before landing fires on landing.
//  * Slope assist: bigger buffer window while on / just off a slope.
//  * Jump prediction: buffer window grows with fall speed.
//  * Stuck-key failsafe: if the game is holding a button the player already released,
//    the mod releases it.
//
// Presses and releases are ALWAYS passed straight to the game (only an impossible-speed
// duplicate press can be dropped). The mod only adds extra taps on its own.
//
// NOTE: GJBaseGameLayer::queueButton is inline in the 2.2081 Windows bindings and
// cannot be hooked, which is why we hook handleButton instead.

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

    // Wall-clock, used ONLY to refresh the cached settings.
    static double wallMs() {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }

    // Game time in ms, accumulated from the per-step dt of player 1's update.
    static double g_gameMs = 0.0;

    // Set while WE inject a button change, so our own hook lets it through untouched.
    static bool g_injecting = false;

    // ------------------------------------------------------------------
    // Settings (cached for 250 ms)
    // ------------------------------------------------------------------
    struct Config {
        bool enabled = true;
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
        double const now = wallMs();
        if (now - lastLoad > 250.0) {
            lastLoad = now;
            auto* m = Mod::get();
            cfg.enabled = m->getSettingValue<bool>("enabled");
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
    struct Slot {
        bool holding = false;          // what the player's finger/key is doing (as the game reports it)
        bool gameDown = false;         // what the game was last told (by the game itself or by us)
        bool pendingRelease = false;   // we injected a tap and still owe it a release
        double lastPushMs = -1e9;      // game time of last accepted press (double-click fix)
        double lastSlopeMs = -1e9;     // last game time this player was on a slope
        double changedMs = -1e9;       // game time gameDown last changed
        double injectedAtMs = -1e9;    // game time of our injected press
        std::deque<double> buffered;   // game times of clicks made while airborne

        void reset() {
            holding = false;
            gameDown = false;
            pendingRelease = false;
            lastPushMs = -1e9;
            lastSlopeMs = -1e9;
            changedMs = -1e9;
            injectedAtMs = -1e9;
            buffered.clear();
        }
    };

    static Slot g_slots[2];

    // Gamemodes that can only act while touching ground (cube, robot, spider).
    // Ship, UFO, ball, wave and swing react in mid-air, so they are never buffered.
    static bool isGroundMode(PlayerObject* p) {
        return !(p->m_isShip || p->m_isBird || p->m_isBall || p->m_isDart || p->m_isSwing);
    }

    // Sends a press/release to the game ourselves (bypasses our own hook logic).
    static void inject(PlayLayer* pl, Slot& s, bool down, bool isPlayer1) {
        g_injecting = true;
        pl->handleButton(down, 1, isPlayer1);
        g_injecting = false;
        s.gameDown = down;
        s.changedMs = g_gameMs;
    }

}

using namespace instantjump;

// ----------------------------------------------------------------------
// The game calls this whenever it applies a press/release (button 1 = jump/fly).
// ----------------------------------------------------------------------
class $modify(InstantJumpGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* pl = PlayLayer::get();
        if (g_injecting || !pl || button != 1 ||
            static_cast<GJBaseGameLayer*>(this) != static_cast<GJBaseGameLayer*>(pl)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto const& cfg = getConfig();
        if (!cfg.enabled) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto& s = g_slots[isPlayer1 ? 0 : 1];
        double const now = g_gameMs;

        if (down) {
            // Double-click fix: swallow an impossible-speed repeat press (never a release).
            if (cfg.doubleClickFix && (now - s.lastPushMs) < static_cast<double>(cfg.doubleClickMs)) {
                return;
            }
            s.lastPushMs = now;
            s.holding = true;
            s.pendingRelease = false;  // the player took over; never release their hold for them

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
            s.pendingRelease = false;
        }

        // Always pass the real input straight through, immediately.
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        s.gameDown = down;
        s.changedMs = now;
    }
};

// ----------------------------------------------------------------------
// Runs once per physics step for each player. Here we
//  1. advance the game clock,
//  2. finish an injected tap (release it one step later),
//  3. fire buffered clicks on landing (with slope + prediction widening),
//  4. failsafe: release a stuck button.
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

        if (idx == 0) g_gameMs += static_cast<double>(dt) * 1000.0;

        auto const& cfg = getConfig();
        if (!cfg.enabled) return;

        auto& s = g_slots[idx];
        bool const isP1 = (idx == 0);
        double const now = g_gameMs;
        if (this->m_isOnSlope) s.lastSlopeMs = now;

        // --- 1. finish an injected tap: release it at least one step later ---
        if (s.pendingRelease && now > s.injectedAtMs) {
            s.pendingRelease = false;
            if (!s.holding) inject(pl, s, false, isP1);
        }

        // --- 2. buffered clicks -> fire on landing ------------------------
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

            if (grounded && !s.buffered.empty() && !s.pendingRelease) {
                s.buffered.pop_front();
                // If the finger is still down the game re-jumps by itself on landing.
                // Otherwise inject a tap: press now, release one step later.
                if (!s.holding && !s.gameDown) {
                    inject(pl, s, true, isP1);
                    s.pendingRelease = true;
                    s.injectedAtMs = now;
                }
            }
        }

        // --- 3. stuck-key failsafe ------------------------------------------
        // The game believes the button is down, the player let go, and we owe nothing.
        if (s.gameDown && !s.holding && !s.pendingRelease && (now - s.changedMs) > 150.0) {
            inject(pl, s, false, isP1);
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
