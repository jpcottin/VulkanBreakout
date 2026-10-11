#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

// android/log.h is available on-device; the test binary links liblog.
#include "game.h"

// ── helpers ───────────────────────────────────────────────────────────────────

// Portrait phone viewport used by every test.
static const int kW = 1080;
static const int kH = 2400;
static float aspect() { return (float)kW / (float)kH; }

// World -> pixel, matching Game's mapping (x in [-asp,asp], y in [-1,1]).
static float pxOf(float wx) { return (wx / aspect() + 1.0f) * 0.5f * kW; }
static float pyOf(float wy) { return (wy + 1.0f) * 0.5f * kH; }

static void step(Game& g, float seconds, float dt = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds; t += dt) g.update(dt);
}

// Tap = press and release; the game consumes it on the next update().
static void tap(Game& g, float px, float py) {
    g.onPointerDown(0, px, py);
    g.onPointerUp(0);
}

static void openSettings(Game& g) {
    tap(g, pxOf(aspect() - 0.062f), pyOf(-0.82f));   // gear, top-right
    g.update(0.016f);
}

static const char* scratchDir() {
    const char* dir = getenv("TMPDIR");
    if (!dir || !dir[0]) dir = "/data/local/tmp";
    return dir;
}

// ── Initial state ─────────────────────────────────────────────────────────────

TEST(InitialState, StartsOnTitle) {
    Game g;
    g.setViewport(kW, kH);
    EXPECT_TRUE(g.inTitleForTest());
    EXPECT_EQ(g.lives(), 5);
    EXPECT_EQ(g.score(), 0);
    EXPECT_EQ(g.level(), 1);
}

// The wall is laid out for the screen width: fixed-size bricks, as many
// columns as fit between the side margins.
TEST(Wall, ColumnsFollowTheScreenWidth) {
    Game g;
    g.setViewport(kW, kH);                    // portrait phone: asp 0.45
    EXPECT_EQ(g.wallColsForTest(), 7);
    EXPECT_EQ(g.brickTotalForTest(), 5 * 7);  // the title demo wall: five rows
    g.setViewport(2400, 1080);                // landscape: asp 2.22
    EXPECT_EQ(g.wallColsForTest(), 36);
    EXPECT_EQ(g.brickTotalForTest(), 5 * 36);
}

TEST(Title, DemoBallBouncesInsideTheSideWalls) {
    Game g;
    g.setViewport(kW, kH);
    for (int i = 0; i < 60 * 20; i++) {
        g.update(1.0f / 60.0f);
        ASSERT_LE(fabsf(g.demoBallXForTest()), aspect());
        ASSERT_LT(g.demoBallYForTest(), 0.62f);   // never below the paddle line
    }
}

TEST(Update, ClampsHugeDtHitch) {
    // A giant frame is clamped to 0.05 s so the demo ball can't warp through a
    // wall on a hitch: a 1 s frame must move it exactly like a 0.05 s one.
    Game huge; huge.setViewport(kW, kH);
    Game ref;  ref.setViewport(kW, kH);
    huge.update(1.0f);
    ref.update(0.05f);
    EXPECT_FLOAT_EQ(huge.demoBallXForTest(), ref.demoBallXForTest());
    EXPECT_FLOAT_EQ(huge.demoBallYForTest(), ref.demoBallYForTest());
}

// ── Settings ─────────────────────────────────────────────────────────────────

TEST(Settings, GearTapOpensSettingsFromTitle) {
    Game g;
    g.setViewport(kW, kH);
    openSettings(g);
    EXPECT_TRUE(g.inSettingsForTest());
}

TEST(Settings, SoundRowToggles) {
    Game g;
    g.setViewport(kW, kH);
    openSettings(g);
    ASSERT_TRUE(g.inSettingsForTest());
    ASSERT_TRUE(g.soundEnabledForTest());
    tap(g, pxOf(0.0f), pyOf(-0.15f));           // sound row
    g.update(0.016f);
    EXPECT_FALSE(g.soundEnabledForTest());
    tap(g, pxOf(0.0f), pyOf(-0.15f));
    g.update(0.016f);
    EXPECT_TRUE(g.soundEnabledForTest());
}

TEST(Settings, BackReturnsToPreviousState) {
    Game g;
    g.setViewport(kW, kH);
    openSettings(g);
    ASSERT_TRUE(g.inSettingsForTest());
    tap(g, pxOf(0.0f), pyOf(0.66f));            // back button
    g.update(0.016f);
    EXPECT_TRUE(g.inTitleForTest());
}

TEST(Settings, GearTapIsIgnoredInsideSettings) {
    Game g;
    g.setViewport(kW, kH);
    openSettings(g);
    ASSERT_TRUE(g.inSettingsForTest());
    openSettings(g);                            // the gear is not drawn here
    EXPECT_TRUE(g.inSettingsForTest());
}

TEST(Settings, PersistAcrossInstances) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/settings.bin", dir);
    remove(path);

    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir);
        openSettings(g);
        tap(g, pxOf(0.0f), pyOf(-0.15f));       // sound off
        g.update(0.016f);
        ASSERT_FALSE(g.soundEnabledForTest());
    }
    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir);
        EXPECT_FALSE(g.soundEnabledForTest());
    }
    remove(path);
}

TEST(Settings, ReloadFromDiskAdoptsTheOtherInstancesChange) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/settings.bin", dir);
    remove(path);

    Game b; b.setViewport(kW, kH); b.setDataPath(dir);   // loads the defaults
    {
        Game a; a.setViewport(kW, kH); a.setDataPath(dir);
        openSettings(a);
        tap(a, pxOf(0.0f), pyOf(-0.15f));       // a turns sound off
        a.update(0.016f);
        ASSERT_FALSE(a.soundEnabledForTest());
    }
    EXPECT_TRUE(b.soundEnabledForTest());       // b hasn't seen it yet
    b.reloadFromDisk();                         // phone<->glasses handoff
    EXPECT_FALSE(b.soundEnabledForTest());
    remove(path);
}

// ── AI Glasses plumbing ──────────────────────────────────────────────────────

TEST(Glasses, NoGearOnTheGlasses) {
    Game g;
    g.setViewport(kW, kH);
    g.setControlMode(Game::CONTROL_TOUCHBAR);
    openSettings(g);                            // gear position
    EXPECT_FALSE(g.inSettingsForTest());
}

TEST(Glasses, TouchbarClearColorIsPureBlack) {
    Game g;
    g.setViewport(kW, kH);
    g.setControlMode(Game::CONTROL_TOUCHBAR);
    float c[3];
    g.clearColor(c);
    EXPECT_FLOAT_EQ(c[0], 0.0f);
    EXPECT_FLOAT_EQ(c[1], 0.0f);
    EXPECT_FLOAT_EQ(c[2], 0.0f);
}

TEST(Glasses, PhoneClearColorIsNotBlack) {
    Game g;
    g.setViewport(kW, kH);
    float c[3];
    g.clearColor(c);
    EXPECT_GT(c[0] + c[1] + c[2], 0.0f);
}

// ── Render coverage ───────────────────────────────────────────────────────────

// Drive render() through every reachable state and require output, so a
// crash in the drawing code shows up here instead of on a device.
TEST(Render, ProducesOutputInEveryReachableState) {
    Game g;
    g.setViewport(kW, kH);
    std::vector<DrawCmd> cmds;

    step(g, 0.5f);
    g.render(cmds);                                    // TITLE (+ demo wall)
    EXPECT_GT(cmds.size(), 0u) << "TITLE drew nothing";
    size_t titleCmds = cmds.size();

    openSettings(g);
    cmds.clear(); g.render(cmds);                      // SETTINGS overlay
    EXPECT_GT(cmds.size(), titleCmds) << "SETTINGS drew nothing on top";

    // Every quad and disc must stay inside NDC (the wall is centred for the
    // screen width).
    for (const auto& c : cmds) {
        float ext = fabsf(c.mtx[0]) + fabsf(c.mtx[1]);   // NDC half-width
        EXPECT_LE(fabsf(c.tx) + ext, 1.05f) << "shape past the screen edge at tx=" << c.tx;
    }
}

TEST(Render, GlassesModeDrawsNoGearAndNoStrip) {
    Game phone;   phone.setViewport(kW, kH);
    Game glasses; glasses.setViewport(kW, kH);
    glasses.setControlMode(Game::CONTROL_TOUCHBAR);
    std::vector<DrawCmd> a, b;
    phone.render(a);
    glasses.render(b);
    EXPECT_LT(b.size(), a.size());                     // gear + strip are missing
}
