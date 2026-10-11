#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
#include "common.h"

class AudioEngine;

// A brick-breaking game: a paddle, a ball and a wall of coloured bricks.
// World coordinates: x in [-asp, asp], y in [-1, 1], y pointing DOWN (the
// bricks sit at the top, the paddle near the bottom). asp = width/height. The
// renderer divides x by asp, so shapes stay square on screen.
//
// Controls: the bottom strip of the screen (below the paddle) is the control
// zone — while a finger presses there, the paddle slides toward the finger's
// x. Any tap launches a ball that is resting on the paddle.
class Game {
public:
    Game();
    void setViewport(int w, int h);

    // Input (pixel coordinates from the touch screen).
    void onPointerDown(int id, float x, float y);
    void onPointerMove(int id, float x, float y);
    void onPointerUp(int id);
    void onPointersCancel();

    void update(float dt);
    void render(std::vector<DrawCmd>& out);
    void clearColor(float out[3]) const;

    // True on static screens (title / game over / win) where the main loop may
    // throttle the frame rate to save battery — and on the phone for the whole
    // time the game is being played on the glasses, so the phone's swapchain
    // stops competing with the projected display's stream.
    bool isIdleScreen() const {
        return state_ == TITLE || state_ == GAME_OVER || state_ == WIN ||
               (glassesActive_ && controlMode_ == CONTROL_STRIP);
    }

public:
    static const int kMaxScores = 5;

    // Brick kinds. Hits to break: 1, 2, 3; steel never breaks and does not
    // count toward clearing the level.
    enum BrickType { BRICK_NORMAL = 0, BRICK_TOUGH = 1, BRICK_ARMOR = 2, BRICK_STEEL = 3 };

    // How touch input drives the paddle.
    //  CONTROL_STRIP:    phone — the strip below the paddle steers.
    //  CONTROL_TOUCHBAR: AI glasses — the whole touch surface is the bar:
    //                    finger x steers. No strip, no gear, pure-black clear
    //                    (black = transparent on AR lenses).
    enum ControlMode { CONTROL_STRIP = 0, CONTROL_TOUCHBAR = 1 };
    void setControlMode(ControlMode m) { controlMode_ = m; }

    // Glasses session plumbing, fed by the platform layer (main.cpp):
    // connection state is polled from the Kotlin bridge; "active" mirrors the
    // process-wide session flag while the game runs on the glasses.
    void setGlassesConnected(bool v) { glassesConnected_ = v; }
    void setGlassesActive(bool v);
    void setGlassesLaunchCallback(std::function<bool()> fn) { glassesLaunch_ = std::move(fn); }
    void setGlassesExitCallback(std::function<void()> fn)   { glassesExit_ = std::move(fn); }

private:
    enum State { TITLE, PLAYING, LEVEL_CLEAR, GAME_OVER, WIN, SETTINGS };

    // One brick of the wall. Its world position derives from its (row, col)
    // slot; the wall is centred horizontally.
    struct Brick {
        int  row = 0, col = 0;
        BrickType type = BRICK_NORMAL;
        int  hp = 1;
        bool alive = false;
    };
    struct Pointer { bool active = false; float x = 0.0f, y = 0.0f; };
    struct HighScore { long score = 0; int level = 0; };

    // --- helpers ---
    float frand();
    float frange(float a, float b);
    void  buildDemoWall();
    int   wallCols() const;
    float brickX(const Brick& b) const;
    float brickY(const Brick& b) const;
    bool  pointerInControlZone(const Pointer& p) const;
    bool  isGearTap(float px, float py) const;
    static int numDigits(long v);

    // emit one shape: world centre (wx,wy), world half-extents (sx,sy), rotation, rgba.
    // style selects the fragment-shader fill (STYLE_FLAT default, STYLE_GLOW).
    void emit(std::vector<DrawCmd>& out, int shape, float wx, float wy,
              float sx, float sy, float rot, float r, float g, float b, float a,
              float style = (float)STYLE_FLAT) const;
    void drawDigit(std::vector<DrawCmd>& out, int d, float cx, float cy,
                   float h, float r, float g, float b, float a) const;
    void drawNumber(std::vector<DrawCmd>& out, long value, float leftX, float cy,
                    float h, float r, float g, float b, float a) const;
    void drawLetter(std::vector<DrawCmd>& out, char ch, float cx, float cy,
                    float h, float r, float g, float b, float a) const;
    void drawText(std::vector<DrawCmd>& out, const char* text, float cx, float cy,
                  float h, float r, float g, float b, float a) const;
    void drawBrick(std::vector<DrawCmd>& out, const Brick& b, float alpha) const;
    void drawPaddle(std::vector<DrawCmd>& out, float cx, float cy, float halfW,
                    float alpha) const;
    void drawBall(std::vector<DrawCmd>& out, float cx, float cy, float alpha) const;
    void drawGearIcon(std::vector<DrawCmd>& out, float cx, float cy, float size,
                      float r, float g, float b, float a) const;
    void drawSettingsScreen(std::vector<DrawCmd>& out) const;
    void loadSettings();
    void saveSettings();
    // Push soundEnabled_ + the current state into the audio engine's music flag.
    void syncMusic();

    // --- audio ---
    AudioEngine* audio_ = nullptr;

    // --- haptic ---
    std::function<void()> haptic_;

    // --- persistence ---
    HighScore highScores_[kMaxScores] = {};
    char dataPath_[512]     = {};
    char settingsPath_[512] = {};

public:
    void setAudioEngine(AudioEngine* a) { audio_ = a; syncMusic(); }
    void setDataPath(const char* path);
    // Randomize the xorshift state (production only — tests rely on the fixed
    // default seed for deterministic choreography). Zero would lock xorshift
    // at zero forever, so it is mapped away.
    void seedRng(uint32_t seed) { rng_ = seed ? seed : 0x1234567u; }
    // Re-read settings from disk (phone <-> glasses handoff).
    void reloadFromDisk();
    // Process-death save/restore (see SavedGame in main.cpp).
    bool isMidGame() const { return state_ == PLAYING || state_ == LEVEL_CLEAR; }
    void restoreSession(int level, long score, int lives);
    void setHapticCallback(std::function<void()> fn) { haptic_ = std::move(fn); }

    // --- test / debug accessors ---
public:
    long  score() const { return score_; }
    int   lives() const { return lives_; }
    int   level() const { return level_; }

#ifndef NDEBUG
    // Test-only helpers — excluded from release builds (NDEBUG defined).
    bool  inTitleForTest()        const { return state_ == TITLE; }
    bool  inSettingsForTest()     const { return state_ == SETTINGS; }
    bool  isPlayingForTest()      const { return state_ == PLAYING; }
    bool  soundEnabledForTest()   const { return soundEnabled_; }
    int   controlModeForTest()    const { return (int)controlMode_; }
    bool  glassesActiveForTest()  const { return glassesActive_; }
    int   wallColsForTest()       const { return wallCols(); }
    int   brickTotalForTest()     const { return (int)bricks_.size(); }
    float demoBallXForTest()      const { return demoBallX_; }
    float demoBallYForTest()      const { return demoBallY_; }
#endif

private:
    // --- viewport ---
    int vw_ = 1, vh_ = 1;
    float asp_ = 0.5f;

    // --- input ---
    static const int kMaxPointers = 16;
    Pointer pointers_[kMaxPointers] = {};
    bool  tapPending_ = false;
    float tapX_       = 0.0f;
    float tapY_       = 0.0f;

    // --- state ---
    State state_     = TITLE;
    State prevState_ = TITLE;
    float animTime_  = 0.0f;

    // --- the wall ---
    std::vector<Brick> bricks_;

    // --- title demo: a ball bouncing under the title, a paddle chasing it ---
    float demoBallX_ = 0.0f, demoBallY_ = 0.0f;
    float demoBallVx_ = 0.55f, demoBallVy_ = 0.80f;
    float demoPaddleX_ = 0.0f;

    // --- progression / scoring ---
    int level_ = 1;
    int lives_ = 5;
    long score_ = 0;

    // --- settings (persisted) ---
    bool soundEnabled_   = true;
    bool autoPlayActive_ = false;

    // --- control mode / glasses session (not persisted) ---
    ControlMode controlMode_ = CONTROL_STRIP;
    bool glassesConnected_ = false;
    bool glassesActive_    = false;
    std::function<bool()> glassesLaunch_;
    std::function<void()> glassesExit_;

    uint32_t rng_ = 0x1234567u;
};
