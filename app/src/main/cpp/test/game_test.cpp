#include <gtest/gtest.h>
#include <cstring>
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

// A pixel y inside the control strip (bottom 15% of the screen).
static float ctrlPy() { return kH * 0.95f; }

// Geometry shared with game.cpp.
static const float kPaddleY  = 0.62f;
static const float kPaddleHW = 0.100f;
static const float kTopWallY = -0.83f;

static void step(Game& g, float seconds, float dt = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds; t += dt) g.update(dt);
}

static void startPlaying(Game& g) {
    g.setViewport(kW, kH);
    g.triggerNewGameForTest();
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

// Index of the brick at (row, col) — bricks_ is built row-major.
static int slot(const Game& g, int row, int col) { return row * g.wallColsForTest() + col; }

// Drop the ball into the pit, away from the paddle: one life gone.
static void loseABall(Game& g) {
    g.setBallForTest(0.40f, 0.70f, 0.0f, 1.0f);
    step(g, 0.3f);
}

// Drive a game straight to a game-over at the given score, which saves the
// high-score table to disk. ASSERTs live in a void helper (legal in gtest).
static void gameOverAtScore(Game& g, long s) {
    ASSERT_TRUE(g.isPlayingForTest());
    g.setScoreForTest(s);
    g.setLivesForTest(1);
    loseABall(g);
    ASSERT_TRUE(g.isGameOverForTest());
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
}

TEST(InitialState, TapStartsGame) {
    Game g;
    g.setViewport(kW, kH);
    tap(g, kW * 0.5f, kH * 0.5f);
    g.update(0.016f);
    EXPECT_TRUE(g.isPlayingForTest());
    EXPECT_EQ(g.lives(), 5);
    EXPECT_EQ(g.score(), 0);
    EXPECT_EQ(g.level(), 1);
    EXPECT_TRUE(g.ballHeldForTest());             // the ball waits on the paddle
    EXPECT_EQ(g.ballCount(), 1);
    EXPECT_FLOAT_EQ(g.paddleX(), 0.0f);
}

// ── The wall ──────────────────────────────────────────────────────────────────

// The wall is laid out for the screen width: fixed-size bricks, as many
// columns as fit between the side margins.
TEST(Wall, ColumnsFollowTheScreenWidth) {
    Game g;
    g.setViewport(kW, kH);                    // portrait phone: asp 0.45
    EXPECT_EQ(g.wallColsForTest(), 7);
    EXPECT_EQ(g.brickTotalForTest(), 5 * 7);  // the title demo wall: five rows
    g.setViewport(2152, 2076);                // unfolded foldable: asp 1.04
    EXPECT_EQ(g.wallColsForTest(), 10);       // the lane caps the wall at 10 columns
    EXPECT_EQ(g.brickTotalForTest(), 5 * 10);
    g.setViewport(2400, 1080);                // landscape: asp 2.22
    EXPECT_EQ(g.wallColsForTest(), 10);
}

TEST(Wall, Level1IsFourRowsOfNormalBricks) {
    Game g;
    startPlaying(g);
    EXPECT_EQ(g.wallRowsForTest(), 4);
    EXPECT_EQ(g.brickTotalForTest(), 4 * 7);
    EXPECT_EQ(g.breakableLeftForTest(), 28);
    for (int i = 0; i < g.brickTotalForTest(); i++) {
        EXPECT_TRUE(g.brickAliveForTest(i));
        EXPECT_EQ(g.brickTypeForTest(i), (int)Game::BRICK_NORMAL);
        EXPECT_EQ(g.brickHpForTest(i), 1);
    }
}

TEST(Wall, RowsGrowWithLevel) {
    Game g;
    g.setViewport(kW, kH);
    const int expected[10] = {4, 5, 5, 6, 6, 7, 7, 8, 8, 9};
    for (int L = 1; L <= 10; L++) {
        g.startLevelForTest(L);
        EXPECT_EQ(g.wallRowsForTest(), expected[L - 1]) << "level " << L;
        EXPECT_EQ(g.level(), L);
    }
}

TEST(Wall, EveryWallFitsTheScreen) {
    Game g;
    g.setViewport(kW, kH);
    for (int L = 1; L <= 10; L++) {
        g.startLevelForTest(L);
        for (int i = 0; i < g.brickTotalForTest(); i++) {
            EXPECT_LT(fabsf(g.brickXForTest(i)) + 0.055f, aspect()) << "level " << L;
            EXPECT_GT(g.brickYForTest(i) - 0.021f, kTopWallY) << "level " << L;
            EXPECT_LT(g.brickYForTest(i), 0.0f) << "level " << L;   // well above the paddle
        }
    }
}

TEST(Wall, TougherBricksArriveWithTheLevels) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(3);
    EXPECT_EQ(g.brickTypeForTest(slot(g, 0, 0)), (int)Game::BRICK_TOUGH);
    EXPECT_EQ(g.brickHpForTest(slot(g, 0, 0)), 2);
    EXPECT_EQ(g.brickTypeForTest(slot(g, 1, 0)), (int)Game::BRICK_NORMAL);
    g.startLevelForTest(7);
    EXPECT_EQ(g.brickTypeForTest(slot(g, 0, 0)), (int)Game::BRICK_ARMOR);
    EXPECT_EQ(g.brickHpForTest(slot(g, 0, 0)), 3);
    EXPECT_EQ(g.brickTypeForTest(slot(g, 1, 0)), (int)Game::BRICK_TOUGH);
}

TEST(Wall, SteelDoesNotCountTowardTheClear) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(5);                   // row 3: steel at col 1, 4
    int steel = 0;
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (g.brickTypeForTest(i) == (int)Game::BRICK_STEEL) steel++;
    EXPECT_EQ(steel, 2);
    EXPECT_EQ(g.breakableLeftForTest(), g.brickTotalForTest() - steel);
    EXPECT_EQ(g.brickTypeForTest(slot(g, 3, 1)), (int)Game::BRICK_STEEL);
}

TEST(Wall, Level4BottomRowHasHoles) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(4);
    int bottom = g.wallRowsForTest() - 1;
    EXPECT_TRUE(g.brickAliveForTest(slot(g, bottom, 0)));
    EXPECT_FALSE(g.brickAliveForTest(slot(g, bottom, 1)));
    EXPECT_TRUE(g.brickAliveForTest(slot(g, bottom, 2)));
}

TEST(Wall, LevelClampsAtTen) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(11);
    EXPECT_EQ(g.level(), 10);
    EXPECT_EQ(g.wallRowsForTest(), 9);
}

// ── Serve and launch ─────────────────────────────────────────────────────────

TEST(Serve, BallRestsOnThePaddleUntilLaunched) {
    Game g;
    startPlaying(g);
    step(g, 1.0f);                              // nothing pressed, nothing tapped
    EXPECT_TRUE(g.ballHeldForTest());
    EXPECT_NEAR(g.ballXForTest(0), g.paddleX(), 1e-5f);
    EXPECT_LT(g.ballYForTest(0), kPaddleY);
}

TEST(Serve, HeldBallFollowsThePaddle) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, pxOf(0.30f), ctrlPy());
    g.update(1.0f / 60.0f);                     // within the serve delay: no launch
    g.onPointerUp(0);
    step(g, 0.3f);
    ASSERT_TRUE(g.ballHeldForTest());
    EXPECT_GT(g.paddleX(), 0.0f);
    EXPECT_NEAR(g.ballXForTest(0), g.paddleX(), 1e-5f);
}

TEST(Serve, EarlyTapIsIgnored) {
    Game g;
    startPlaying(g);
    tap(g, kW * 0.5f, kH * 0.5f);               // right at the serve
    g.update(1.0f / 60.0f);
    EXPECT_TRUE(g.ballHeldForTest());
}

TEST(Serve, TapLaunchesAfterTheServeDelay) {
    Game g;
    startPlaying(g);
    step(g, 0.6f);
    tap(g, kW * 0.5f, kH * 0.5f);
    g.update(1.0f / 60.0f);
    EXPECT_FALSE(g.ballHeldForTest());
    EXPECT_LT(g.ballDyForTest(0), -0.5f);       // upward
    EXPECT_GT(fabsf(g.ballDxForTest(0)), 0.2f); // and to one side
}

TEST(Serve, HoldingTheStripLaunchesToo) {
    Game g;
    startPlaying(g);
    step(g, 0.6f);
    g.onPointerDown(0, pxOf(0.0f), ctrlPy());
    g.update(1.0f / 60.0f);
    EXPECT_FALSE(g.ballHeldForTest());
}

// ── Paddle control (finger in the bottom strip) ───────────────────────────────

TEST(PaddleControl, MovesTowardFingerInStrip) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kW * 0.85f, ctrlPy());
    step(g, 0.3f);
    EXPECT_GT(g.paddleX(), 0.05f);
}

TEST(PaddleControl, IgnoresFingerAboveStrip) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, kW * 0.85f, kH * 0.5f);   // mid-screen, not the strip
    step(g, 0.3f);
    EXPECT_FLOAT_EQ(g.paddleX(), 0.0f);
}

TEST(PaddleControl, StopsOnTheFingerX) {
    Game g;
    startPlaying(g);
    float targetWx = 0.2f;
    g.onPointerDown(0, pxOf(targetWx), ctrlPy());
    step(g, 1.5f);
    EXPECT_NEAR(g.paddleX(), targetWx, 0.02f);
}

TEST(PaddleControl, ClampedToScreenEdge) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, (float)kW, ctrlPy());
    step(g, 2.0f);
    EXPECT_LE(g.paddleX() + kPaddleHW, aspect() + 1e-5f);
}

// A mid-session viewport change (fold/unfold, projected display) must clamp
// the paddle back inside the new, narrower playfield.
TEST(PaddleControl, ViewportShrinkClampsThePaddle) {
    Game g;
    startPlaying(g);                            // 1080x2400: asp = 0.45
    g.setPaddleXForTest(aspect() - 0.11f);      // parked near the right edge
    g.setViewport(720, 2400);                   // asp shrinks to 0.30
    EXPECT_LE(g.paddleX() + kPaddleHW, 0.30f + 1e-5f);
}

// Dragging inside the strip is the primary steering gesture on the phone;
// the paddle must follow the moved finger, not the original touch-down point.
TEST(PaddleControl, DragSteersThePaddle) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, pxOf(0.0f), ctrlPy());
    step(g, 0.5f);
    ASSERT_NEAR(g.paddleX(), 0.0f, 0.02f);
    g.onPointerMove(0, pxOf(0.30f), ctrlPy());
    step(g, 1.5f);
    EXPECT_NEAR(g.paddleX(), 0.30f, 0.02f);
}

// A system cancel (e.g. gesture-nav interception) must release all pointers:
// no phantom finger may keep steering.
TEST(PaddleControl, CancelReleasesAllPointers) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, pxOf(0.30f), ctrlPy());
    step(g, 0.05f);                // paddle starts walking toward the finger
    g.onPointersCancel();
    float x = g.paddleX();
    step(g, 1.0f);
    EXPECT_FLOAT_EQ(g.paddleX(), x);
}

TEST(PaddleControl, NoTouchNoMovement) {
    Game g;
    startPlaying(g);
    step(g, 0.5f);
    EXPECT_FLOAT_EQ(g.paddleX(), 0.0f);
}

TEST(Update, ClampsHugeDtHitch) {
    // A giant frame is clamped to 0.05 s so the paddle can't warp on a hitch.
    // Same held finger: a 1 s frame must move it exactly like a 0.05 s one.
    Game huge; startPlaying(huge);
    Game ref;  startPlaying(ref);
    huge.onPointerDown(0, pxOf(0.30f), ctrlPy());   // steer right, out of reach
    ref.onPointerDown(0, pxOf(0.30f), ctrlPy());    // in a single clamped frame
    huge.update(1.0f);
    ref.update(0.05f);
    EXPECT_FLOAT_EQ(huge.paddleX(), ref.paddleX());
    EXPECT_GT(huge.paddleX(), 0.0f);
    EXPECT_LT(huge.paddleX(), 0.30f);
}

// ── Ball physics ─────────────────────────────────────────────────────────────

TEST(Ball, BouncesOffTheSideWalls) {
    Game g;
    startPlaying(g);
    g.setBallForTest(aspect() - 0.05f, 0.20f, 1.0f, -0.3f);   // heading right, near the wall
    step(g, 0.2f);
    EXPECT_LT(g.ballDxForTest(0), 0.0f);
    EXPECT_LE(g.ballXForTest(0) + 0.016f, aspect() + 1e-4f);
    g.setBallForTest(-aspect() + 0.05f, 0.20f, -1.0f, -0.3f);
    step(g, 0.2f);
    EXPECT_GT(g.ballDxForTest(0), 0.0f);
}

TEST(Ball, BouncesOffTheCeiling) {
    Game g;
    startPlaying(g);
    // Clear the ball's column up to the top; one brick stays so the level
    // does not end under the test.
    int keep = slot(g, 3, 0);
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (i != keep) g.killBrickForTest(i);
    g.setBallForTest(0.0f, kTopWallY + 0.05f, 0.0f, -1.0f);
    step(g, 0.1f);
    EXPECT_GT(g.ballDyForTest(0), 0.0f);
    EXPECT_GE(g.ballYForTest(0) - 0.016f, kTopWallY - 1e-4f);
}

TEST(Ball, BouncesOffThePaddle) {
    Game g;
    startPlaying(g);
    g.setBallForTest(0.0f, kPaddleY - 0.10f, 0.0f, 1.0f);   // falling onto the paddle
    step(g, 0.3f);
    EXPECT_LT(g.ballDyForTest(0), 0.0f);
    EXPECT_EQ(g.paddleHitsForTest(), 1);
    EXPECT_EQ(g.lives(), 5);
}

TEST(Ball, PaddleTipSendsTheBallSideways) {
    Game g;
    startPlaying(g);
    g.setBallForTest(0.09f, kPaddleY - 0.10f, 0.0f, 1.0f);   // near the right tip
    step(g, 0.3f);
    ASSERT_EQ(g.paddleHitsForTest(), 1);
    EXPECT_GT(g.ballDxForTest(0), 0.70f);                     // ~58 degrees out
    EXPECT_LT(g.ballDyForTest(0), 0.0f);
}

TEST(Ball, PaddleCentreSendsItNearlyStraightUp) {
    Game g;
    startPlaying(g);
    g.setBallForTest(0.0f, kPaddleY - 0.10f, 0.0f, 1.0f);
    step(g, 0.3f);
    ASSERT_EQ(g.paddleHitsForTest(), 1);
    EXPECT_LT(fabsf(g.ballDxForTest(0)), 0.20f);
    EXPECT_GT(fabsf(g.ballDxForTest(0)), 0.10f);              // never exactly vertical
}

TEST(Ball, PassingThePaddleCostsALifeAndServesAgain) {
    Game g;
    startPlaying(g);
    loseABall(g);
    EXPECT_EQ(g.lives(), 4);
    EXPECT_EQ(g.ballsLostForTest(), 1);
    EXPECT_TRUE(g.isPlayingForTest());
    EXPECT_TRUE(g.ballHeldForTest());           // a fresh ball on the paddle
    EXPECT_EQ(g.ballCount(), 1);
}

TEST(Ball, LosingTheLastLifeEndsTheGame) {
    Game g;
    startPlaying(g);
    for (int i = 0; i < 5; i++) loseABall(g);
    EXPECT_EQ(g.lives(), 0);
    EXPECT_TRUE(g.isGameOverForTest());
    EXPECT_EQ(g.ballCount(), 0);
}

TEST(Ball, BreaksABrickAndScores) {
    Game g;
    startPlaying(g);
    int target = slot(g, 3, 3);                 // bottom row, middle
    g.setBallForTest(g.brickXForTest(target), g.brickYForTest(target) + 0.10f, 0.0f, -1.0f);
    step(g, 0.3f);
    EXPECT_FALSE(g.brickAliveForTest(target));
    EXPECT_EQ(g.score(), 10L * g.level());
    EXPECT_EQ(g.bricksBrokenForTest(), 1);
    EXPECT_GT(g.ballDyForTest(0), 0.0f);        // and came back down
}

TEST(Ball, ToughBrickTakesTwoHits) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(3);                     // row 0 is tough
    int target = slot(g, 0, 3);
    for (int r = 1; r < g.wallRowsForTest(); r++) g.killBrickForTest(slot(g, r, 3));
    long before = g.score();
    g.setBallForTest(g.brickXForTest(target), g.brickYForTest(target) + 0.10f, 0.0f, -1.0f);
    step(g, 0.2f);
    EXPECT_TRUE(g.brickAliveForTest(target));
    EXPECT_EQ(g.brickHpForTest(target), 1);
    EXPECT_EQ(g.score(), before);               // a dent does not score
    g.setBallForTest(g.brickXForTest(target), g.brickYForTest(target) + 0.10f, 0.0f, -1.0f);
    step(g, 0.2f);
    EXPECT_FALSE(g.brickAliveForTest(target));
    EXPECT_EQ(g.score() - before, 20L * 3);
}

TEST(Ball, SteelBrickNeverBreaks) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(5);                     // row 3: steel at col 1
    int target = slot(g, 3, 1);
    ASSERT_EQ(g.brickTypeForTest(target), (int)Game::BRICK_STEEL);
    for (int r = 4; r < g.wallRowsForTest(); r++) g.killBrickForTest(slot(g, r, 1));
    for (int i = 0; i < 5; i++) {
        g.setBallForTest(g.brickXForTest(target), g.brickYForTest(target) + 0.10f, 0.0f, -1.0f);
        step(g, 0.2f);
        EXPECT_GT(g.ballDyForTest(0), 0.0f);    // bounced back
    }
    EXPECT_TRUE(g.brickAliveForTest(target));
    EXPECT_EQ(g.score(), 0);
}

TEST(Ball, SideHitReflectsHorizontally) {
    Game g;
    startPlaying(g);
    // Clear everything but one brick, then come at its left face sideways.
    int target = slot(g, 3, 3);
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (i != target) g.killBrickForTest(i);
    g.setBallForTest(g.brickXForTest(target) - 0.15f, g.brickYForTest(target), 1.0f, 0.02f);
    step(g, 0.3f);
    EXPECT_FALSE(g.brickAliveForTest(target));
    EXPECT_LT(g.ballDxForTest(0), 0.0f);
}

TEST(Ball, SpeedsUpAsTheWallThins) {
    Game g;
    startPlaying(g);
    float base = g.ballSpeedForTest();
    int target = slot(g, 3, 3);
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (i != target) g.killBrickForTest(i);
    g.setBallForTest(g.brickXForTest(target), g.brickYForTest(target) + 0.10f, 0.0f, -1.0f);
    step(g, 0.2f);
    ASSERT_FALSE(g.brickAliveForTest(target));
    EXPECT_GT(g.ballSpeedForTest(), base * 1.15f);
}

TEST(Ball, FasterWithEveryLevel) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(1);
    float s1 = g.ballSpeedForTest();
    g.startLevelForTest(10);
    EXPECT_GT(g.ballSpeedForTest(), s1 * 1.5f);
}

// Regression guard: at the clamped worst-case dt (0.05 s) the ball moves
// more than its own diameter per frame. Substepped collision must keep it
// inside the walls and breaking bricks instead of tunnelling through them.
TEST(Ball, NeverTunnelsAtTheClampedWorstCaseDt) {
    Game g;
    startPlaying(g);
    g.setBallForTest(0.0f, 0.30f, 0.35f, -1.0f);
    for (int i = 0; i < 600; i++) {
        g.setPaddleXForTest(g.ballXForTest(0));   // the paddle is always underneath
        g.update(0.05f);
        ASSERT_EQ(g.ballCount(), 1) << "ball lost at frame " << i;
        ASSERT_LE(fabsf(g.ballXForTest(0)) + 0.016f, aspect() + 1e-3f) << "frame " << i;
        ASSERT_GE(g.ballYForTest(0) - 0.016f, kTopWallY - 1e-3f) << "frame " << i;
        if (!g.isPlayingForTest()) break;         // the wall is gone
    }
    EXPECT_GT(g.bricksBrokenForTest(), 5);
}

// ── Bonuses ──────────────────────────────────────────────────────────────────

TEST(Bonus, FallingCapsuleIsCollectedByThePaddle) {
    Game g;
    startPlaying(g);
    g.spawnTestPowerUp(g.paddleX(), 0.40f, (int)Game::PU_WIDE);
    step(g, 1.0f);
    EXPECT_TRUE(g.wideActiveForTest());
    EXPECT_EQ(g.score(), 25L * g.level());
    EXPECT_EQ(g.powerUpCountForTest(), 0);
}

TEST(Bonus, UncollectedCapsuleFallsPast) {
    Game g;
    startPlaying(g);
    g.spawnTestPowerUp(g.paddleX() + 0.30f, 0.40f, (int)Game::PU_WIDE);
    step(g, 3.0f);
    EXPECT_FALSE(g.wideActiveForTest());
    EXPECT_EQ(g.score(), 0);
    EXPECT_EQ(g.powerUpCountForTest(), 0);
}

TEST(Bonus, WidePaddleWidensThenExpires) {
    Game g;
    startPlaying(g);
    float normal = g.paddleHalfWForTest();
    g.activatePowerUpForTest((int)Game::PU_WIDE);
    EXPECT_GT(g.paddleHalfWForTest(), normal * 1.4f);
    step(g, 11.0f);
    EXPECT_FALSE(g.wideActiveForTest());
    EXPECT_FLOAT_EQ(g.paddleHalfWForTest(), normal);
}

TEST(Bonus, WidePaddleExpiringNearTheEdgeIsClampedBack) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest((int)Game::PU_WIDE);
    g.setPaddleXForTest(aspect() - 0.16f);      // flush with the edge, wide
    step(g, 11.0f);
    EXPECT_LE(g.paddleX() + kPaddleHW, aspect() + 1e-5f);
}

TEST(Bonus, SlowBallSlowsThenExpires) {
    Game g;
    startPlaying(g);
    float normal = g.ballSpeedForTest();
    g.activatePowerUpForTest((int)Game::PU_SLOW);
    EXPECT_NEAR(g.ballSpeedForTest(), normal * 0.6f, 1e-4f);
    step(g, 9.0f);
    EXPECT_FALSE(g.slowActiveForTest());
    EXPECT_FLOAT_EQ(g.ballSpeedForTest(), normal);
}

TEST(Bonus, MultiBallSplitsTheBallInThree) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest((int)Game::PU_MULTI);
    EXPECT_FALSE(g.ballHeldForTest());          // a held ball is launched first
    EXPECT_EQ(g.ballCount(), 3);
    for (int i = 0; i < 3; i++) EXPECT_LT(g.ballDyForTest(i), 0.0f);
    // The three fan out.
    EXPECT_NE(g.ballDxForTest(1), g.ballDxForTest(2));
}

TEST(Bonus, MultiBallCapsAtSix) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest((int)Game::PU_MULTI);
    g.activatePowerUpForTest((int)Game::PU_MULTI);
    g.activatePowerUpForTest((int)Game::PU_MULTI);
    EXPECT_EQ(g.ballCount(), 6);
}

TEST(Bonus, LosingOneOfSeveralBallsCostsNoLife) {
    Game g;
    startPlaying(g);
    int keep = slot(g, 3, 0);                    // one brick stays: the level goes on
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (i != keep) g.killBrickForTest(i);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);   // one ball flying up
    g.addBallForTest(0.40f, 0.70f, 0.0f, 1.0f);  // one dropping into the pit
    step(g, 0.3f);
    EXPECT_EQ(g.ballCount(), 1);
    EXPECT_EQ(g.lives(), 5);
}

TEST(Bonus, ExtraLifeAddsOneUpToNine) {
    Game g;
    startPlaying(g);
    g.spawnTestPowerUp(g.paddleX(), 0.40f, (int)Game::PU_LIFE);
    step(g, 1.0f);
    EXPECT_EQ(g.lives(), 6);
    for (int i = 0; i < 10; i++) g.activatePowerUpForTest((int)Game::PU_LIFE);
    EXPECT_EQ(g.lives(), 9);
}

TEST(Bonus, TimedBonusesEndWithTheBall) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest((int)Game::PU_WIDE);
    g.activatePowerUpForTest((int)Game::PU_SLOW);
    loseABall(g);
    EXPECT_FALSE(g.wideActiveForTest());
    EXPECT_FALSE(g.slowActiveForTest());
}

// ── Level clear / win ─────────────────────────────────────────────────────────

TEST(Progression, ClearingTheWallPaysABonusAndAdvances) {
    Game g;
    startPlaying(g);
    g.spawnTestPowerUp(0.3f, 0.0f, (int)Game::PU_WIDE);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);
    long before = g.score();
    g.clearWallForTest();
    g.update(0.016f);
    EXPECT_TRUE(g.isLevelClearForTest());
    EXPECT_EQ(g.score() - before, 100L * 1);
    EXPECT_EQ(g.ballCount(), 0);                // in-flight balls and capsules don't cross levels
    EXPECT_EQ(g.powerUpCountForTest(), 0);
    step(g, 2.0f);
    EXPECT_TRUE(g.isPlayingForTest());
    EXPECT_EQ(g.level(), 2);
    EXPECT_EQ(g.wallRowsForTest(), 5);
    EXPECT_TRUE(g.ballHeldForTest());
}

TEST(Progression, LevelClearBonusScalesWithLevel) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(9);
    long before = g.score();
    g.clearWallForTest();
    g.update(0.016f);
    ASSERT_TRUE(g.isLevelClearForTest());
    EXPECT_EQ(g.score() - before, 100L * 9);
}

TEST(Progression, ClearingLevelTenWins) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(10);
    g.setScoreForTest(500);
    g.clearWallForTest();
    g.update(0.016f);
    EXPECT_TRUE(g.isWinForTest());
    EXPECT_EQ(g.score(), 500 + 100L * 10);
    // The table is written on the WIN transition.
    EXPECT_EQ(g.highScoreForTest(0), g.score());
}

TEST(Progression, GameOverTapReturnsToTitle) {
    Game g;
    startPlaying(g);
    g.setLivesForTest(1);
    loseABall(g);
    ASSERT_TRUE(g.isGameOverForTest());
    step(g, 1.0f);
    tap(g, kW * 0.5f, kH * 0.5f);
    g.update(0.016f);
    EXPECT_TRUE(g.inTitleForTest());
}

TEST(Progression, GameOverIgnoresEarlyTap) {
    Game g;
    startPlaying(g);
    g.setLivesForTest(1);
    loseABall(g);
    ASSERT_TRUE(g.isGameOverForTest());
    tap(g, kW * 0.5f, kH * 0.5f);               // within the 0.6 s grace
    g.update(0.016f);
    EXPECT_TRUE(g.isGameOverForTest());         // ignored: don't skip the death screen
    step(g, 0.7f);
    tap(g, kW * 0.5f, kH * 0.5f);               // after the grace
    g.update(0.016f);
    EXPECT_TRUE(g.inTitleForTest());
}

TEST(Progression, TitleReturnClearsLeftovers) {
    Game g;
    startPlaying(g);
    g.activatePowerUpForTest((int)Game::PU_WIDE);
    g.spawnTestPowerUp(0.2f, 0.0f, (int)Game::PU_SLOW);
    g.setLivesForTest(1);
    loseABall(g);
    ASSERT_TRUE(g.isGameOverForTest());
    ASSERT_EQ(g.powerUpCountForTest(), 1);      // still falling on the end screen
    step(g, 0.7f);
    tap(g, kW * 0.5f, kH * 0.5f);
    g.update(0.016f);
    ASSERT_TRUE(g.inTitleForTest());
    EXPECT_EQ(g.powerUpCountForTest(), 0);      // wiped so nothing renders frozen
    EXPECT_EQ(g.ballCount(), 0);
    EXPECT_FALSE(g.wideActiveForTest());
    EXPECT_EQ(g.brickTotalForTest(), 5 * 7);    // the demo wall is back
    EXPECT_EQ(g.level(), 1);
}

// A fold/unfold mid-level changes how many columns fit: the wall is rebuilt
// for the new width, score and lives stay, and a fresh ball is served.
TEST(Progression, FoldMidLevelRebuildsTheWall) {
    Game g;
    startPlaying(g);
    g.setScoreForTest(70);
    g.setLivesForTest(3);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);
    ASSERT_EQ(g.wallColsForTest(), 7);
    g.setViewport(2152, 2076);                  // unfolded: asp 1.04
    EXPECT_EQ(g.wallColsForTest(), 10);
    EXPECT_EQ(g.brickTotalForTest(), 4 * 10);
    EXPECT_EQ(g.score(), 70);
    EXPECT_EQ(g.lives(), 3);
    EXPECT_TRUE(g.ballHeldForTest());
    EXPECT_TRUE(g.isPlayingForTest());
    for (int i = 0; i < g.brickTotalForTest(); i++)
        EXPECT_LT(fabsf(g.brickXForTest(i)) + 0.055f, 0.63f);   // inside the lane
}

// A wide screen plays in a central lane: the ball and the paddle stay inside
// it, the margins outside are dead space.
TEST(Wall, WideScreenPlaysInACentralLane) {
    Game g;
    g.setViewport(2400, 1080);                  // landscape: asp 2.22, lane 0.63
    g.triggerNewGameForTest();
    g.setBallForTest(0.5f, 0.2f, 1.0f, -0.3f);  // heading for the lane's right wall
    step(g, 0.3f);
    EXPECT_LT(g.ballDxForTest(0), 0.0f);
    EXPECT_LE(g.ballXForTest(0) + 0.016f, 0.63f + 1e-4f);
    g.onPointerDown(0, 2400.0f, 1080.0f * 0.95f);   // finger at the screen's right edge
    step(g, 2.0f);
    EXPECT_LE(g.paddleX() + kPaddleHW, 0.63f + 1e-5f);
}

// ── Process-death resume ─────────────────────────────────────────────────────

TEST(RestoreSession, ResumesLevelScoreAndLives) {
    Game g;
    g.setViewport(kW, kH);
    g.restoreSession(5, 4200, 2);              // OS handed back a mid-run snapshot
    EXPECT_TRUE(g.isPlayingForTest());
    EXPECT_EQ(g.level(), 5);
    EXPECT_EQ(g.score(), 4200);
    EXPECT_EQ(g.lives(), 2);
    EXPECT_EQ(g.wallRowsForTest(), 6);         // level>1 ran startLevel: 6 rows at L5
    EXPECT_TRUE(g.ballHeldForTest());
}

TEST(RestoreSession, RejectsOutOfRangeInput) {
    // The blob comes back from the OS, not from us: any out-of-range field must
    // be rejected wholesale, leaving a fresh game untouched on the title screen.
    for (int badLevel : {0, 11}) {
        Game g; g.setViewport(kW, kH);
        g.restoreSession(badLevel, 100, 2);
        EXPECT_TRUE(g.inTitleForTest()) << "level=" << badLevel;
    }
    { Game g; g.setViewport(kW, kH); g.restoreSession(3, -1,  2); EXPECT_TRUE(g.inTitleForTest()); }
    { Game g; g.setViewport(kW, kH); g.restoreSession(3, 100, 0); EXPECT_TRUE(g.inTitleForTest()); }
    { Game g; g.setViewport(kW, kH); g.restoreSession(3, 100, 10); EXPECT_TRUE(g.inTitleForTest()); }
}

// ── High scores ──────────────────────────────────────────────────────────────

// The table must insert by rank, displace lower entries, and cap at five.
TEST(HighScores, TableOrdersDisplacesAndCaps) {
    Game g;
    startPlaying(g);   // dataPath unset: saves are no-ops, table is in-memory
    const long runs[6] = {300, 100, 500, 200, 400, 250};
    for (long r : runs) {
        gameOverAtScore(g, r);
        step(g, 0.7f);                         // GAME_OVER accepts taps after 0.6 s
        tap(g, kW * 0.5f, kH * 0.5f);
        g.update(1.0f / 60.0f);                // -> TITLE
        ASSERT_TRUE(g.inTitleForTest());
        tap(g, kW * 0.5f, kH * 0.5f);
        g.update(1.0f / 60.0f);                // -> fresh PLAYING run
    }
    EXPECT_EQ(g.highScoreForTest(0), 500);
    EXPECT_EQ(g.highScoreForTest(1), 400);
    EXPECT_EQ(g.highScoreForTest(2), 300);
    EXPECT_EQ(g.highScoreForTest(3), 250);
    EXPECT_EQ(g.highScoreForTest(4), 200);     // the 100 was displaced off
}

TEST(HighScores, SavedOnGameOverAndReloaded) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    remove(path);

    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir);
        g.triggerNewGameForTest();
        gameOverAtScore(g, 1234);
    }
    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir);
        EXPECT_EQ(g.highScoreForTest(0), 1234);
    }
    remove(path);
}

// Two Game objects share the file (as the phone and glasses instances do) but
// never see each other's in-memory table. On save, each must fold the disk
// table into its own so a stale copy can't clobber a score saved meanwhile.
TEST(HighScores, SaveMergesDiskTableWithoutClobbering) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    remove(path);

    Game a; a.setViewport(kW, kH); a.setDataPath(dir); a.triggerNewGameForTest();
    Game b; b.setViewport(kW, kH); b.setDataPath(dir); b.triggerNewGameForTest();
    gameOverAtScore(a, 500);                    // file: {500}
    gameOverAtScore(b, 300);                    // b never loaded 500, yet save keeps it

    Game c; c.setViewport(kW, kH); c.setDataPath(dir);   // read the merged file back
    EXPECT_EQ(c.highScoreForTest(0), 500);
    EXPECT_EQ(c.highScoreForTest(1), 300);
    remove(path);
}

TEST(HighScores, MergeSkipsExactDuplicates) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    remove(path);

    // Both instances end an identical fresh run (score 500, level 1). The shared
    // row must appear once, not doubled.
    Game a; a.setViewport(kW, kH); a.setDataPath(dir); a.triggerNewGameForTest();
    Game b; b.setViewport(kW, kH); b.setDataPath(dir); b.triggerNewGameForTest();
    gameOverAtScore(a, 500);
    gameOverAtScore(b, 500);

    Game c; c.setViewport(kW, kH); c.setDataPath(dir);
    EXPECT_EQ(c.highScoreForTest(0), 500);
    EXPECT_EQ(c.highScoreForTest(1), 0);        // the identical run wasn't doubled
    remove(path);
}

TEST(HighScores, ReloadFromDiskAdoptsPersistedTable) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    remove(path);

    Game b; b.setViewport(kW, kH); b.setDataPath(dir);   // loads empty file
    {
        Game a; a.setViewport(kW, kH); a.setDataPath(dir); a.triggerNewGameForTest();
        gameOverAtScore(a, 700);                // a writes {700} while b is idle
    }
    EXPECT_EQ(b.highScoreForTest(0), 0);        // b hasn't seen it yet
    b.reloadFromDisk();                         // phone<->glasses handoff
    EXPECT_EQ(b.highScoreForTest(0), 700);
    remove(path);
}

TEST(HighScores, ZeroScoreIsNotRecorded) {
    Game g;
    startPlaying(g);
    gameOverAtScore(g, 0);
    EXPECT_EQ(g.highScoreForTest(0), 0);        // a zero run never claims a rank
    EXPECT_FALSE(g.newHighScoreForTest());
}

TEST(HighScores, TieWithExistingEntryDoesNotDuplicate) {
    // checkHighScore() must apply the same dedupe rule as the disk merge, or
    // a tie becomes two identical rows on one instance and none on the other.
    Game g;
    startPlaying(g);
    gameOverAtScore(g, 30);                     // saved 30/1
    ASSERT_EQ(g.highScoreForTest(0), 30L);
    EXPECT_TRUE(g.newHighScoreForTest());
    step(g, 0.7f);
    tap(g, kW * 0.5f, kH * 0.5f);
    g.update(0.016f);                           // -> TITLE
    tap(g, kW * 0.5f, kH * 0.5f);
    g.update(0.016f);                           // -> new round
    gameOverAtScore(g, 30);                     // second 30/1
    EXPECT_EQ(g.highScoreForTest(0), 30L);
    EXPECT_EQ(g.highScoreForTest(1), 0L);       // not duplicated
    EXPECT_FALSE(g.newHighScoreForTest());      // and not celebrated as a new record
}

// ── Settings ─────────────────────────────────────────────────────────────────

TEST(Settings, GearTapOpensSettingsFromTitle) {
    Game g;
    g.setViewport(kW, kH);
    openSettings(g);
    EXPECT_TRUE(g.inSettingsForTest());
}

TEST(Settings, GearTapOpensSettingsWhilePlaying) {
    Game g;
    startPlaying(g);
    openSettings(g);
    EXPECT_TRUE(g.inSettingsForTest());
}

TEST(Settings, GameplayFreezesUnderSettings) {
    Game g;
    startPlaying(g);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);
    openSettings(g);
    float y = g.ballYForTest(0);
    step(g, 1.0f);
    EXPECT_FLOAT_EQ(g.ballYForTest(0), y);
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

TEST(Settings, AutoPlayRowToggles) {
    Game g;
    g.setViewport(kW, kH);
    openSettings(g);
    ASSERT_FALSE(g.autoPlayActiveForTest());
    tap(g, pxOf(0.0f), pyOf(0.10f));            // auto play row
    g.update(0.016f);
    EXPECT_TRUE(g.autoPlayActiveForTest());
    tap(g, pxOf(0.0f), pyOf(0.10f));
    g.update(0.016f);
    EXPECT_FALSE(g.autoPlayActiveForTest());
}

TEST(Settings, BackReturnsToPreviousState) {
    Game g;
    startPlaying(g);
    openSettings(g);
    ASSERT_TRUE(g.inSettingsForTest());
    tap(g, pxOf(0.0f), pyOf(0.66f));            // back button
    g.update(0.016f);
    EXPECT_TRUE(g.isPlayingForTest());
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
        tap(g, pxOf(0.0f), pyOf(0.10f));        // auto play on
        g.update(0.016f);
        ASSERT_FALSE(g.soundEnabledForTest());
        ASSERT_TRUE(g.autoPlayActiveForTest());
    }
    {
        Game g;
        g.setViewport(kW, kH);
        g.setDataPath(dir);
        EXPECT_FALSE(g.soundEnabledForTest());
        EXPECT_TRUE(g.autoPlayActiveForTest());
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

// ── Auto Play ────────────────────────────────────────────────────────────────

TEST(AutoPlay, LaunchesTheHeldBallOnItsOwn) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    step(g, 0.3f);
    EXPECT_TRUE(g.ballHeldForTest());            // the serve delay is respected
    step(g, 0.5f);
    EXPECT_FALSE(g.ballHeldForTest());
}

TEST(AutoPlay, PredictsWhereTheBallLands) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    int keep = slot(g, 3, 0);                     // one brick stays, far from the ball's path
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (i != keep) g.killBrickForTest(i);
    g.setBallForTest(-0.30f, 0.10f, 0.6f, 1.0f);  // falling to the right, nothing in the way
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasLandingForTest());
    float predictedX = g.aiLandingXForTest();
    float predictedT = g.aiLandingTForTest();
    // Let it fall with the paddle parked out of the way and see where it was
    // when it crossed the paddle line.
    g.setAutoPlayForTest(false);
    float actualX = 0.0f, actualT = 0.0f;
    for (int i = 0; i < 300; i++) {
        g.setPaddleXForTest(0.60f - 0.1f);        // far right, away from the ball
        float yBefore = g.ballCount() ? g.ballYForTest(0) : 9.0f;
        g.update(1.0f / 60.0f);
        actualT += 1.0f / 60.0f;
        if (g.ballCount() == 0 || g.ballYForTest(0) + 0.016f >= kPaddleY - 0.014f) {
            actualX = g.ballCount() ? g.ballXForTest(0) : actualX;
            (void)yBefore;
            break;
        }
        actualX = g.ballXForTest(0);
    }
    EXPECT_NEAR(predictedX, actualX, 0.03f);
    EXPECT_NEAR(predictedT, actualT, 0.10f);
}

TEST(AutoPlay, GoesUnderTheBall) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    g.setBallForTest(0.30f, 0.20f, 0.0f, 1.0f);   // dropping straight down at x 0.30
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasTargetForTest());
    EXPECT_NEAR(g.aiTargetXForTest(), 0.30f, kPaddleHW);   // within a paddle of the landing
    step(g, 1.0f);
    EXPECT_EQ(g.lives(), 5);
    EXPECT_GE(g.paddleHitsForTest(), 1);
}

TEST(AutoPlay, CatchesABallComingOffTheWall) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    g.setBallForTest(0.35f, 0.30f, 1.0f, 0.5f);   // heading for the right wall, then back
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasLandingForTest());
    EXPECT_LT(g.aiLandingXForTest(), 0.35f);      // it comes down left of where it is now
    step(g, 1.5f);
    EXPECT_EQ(g.lives(), 5);
    EXPECT_GE(g.paddleHitsForTest(), 1);
}

TEST(AutoPlay, AimsTheReboundAtTheLastBrick) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    // One brick left, far to the left; the ball drops straight down on the
    // right. A centre hit would send it back up the empty right side for
    // ever: the plan takes it off-centre so a rebound reaches the brick —
    // within a couple of trips, not by luck.
    int keep = slot(g, 3, 0);
    for (int i = 0; i < g.brickTotalForTest(); i++)
        if (i != keep) g.killBrickForTest(i);
    g.setBallForTest(0.30f, 0.10f, 0.0f, 1.0f);
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasLandingForTest());
    EXPECT_NE(g.aiTargetXForTest(), 0.30f);       // not a centre hit
    for (int i = 0; i < 60 * 12 && g.isPlayingForTest(); i++) g.update(1.0f / 60.0f);
    EXPECT_TRUE(g.isLevelClearForTest()) << "the last brick is still up after 12 s";
    EXPECT_EQ(g.lives(), 5);
}

TEST(AutoPlay, NeverPlansADeadCentreHit) {
    // |u| < 0.12 all leave at the 8-degree minimum, left or right by the sign
    // of a hair's width of paddle placement: the plan never lives there.
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    for (int i = 0; i < 20; i++) {
        g.setBallForTest(-0.3f + 0.03f * (float)i, 0.10f, 0.0f, 1.0f);
        g.update(1.0f / 60.0f);
        ASSERT_TRUE(g.aiHasLandingForTest());
        EXPECT_GE(fabsf(g.aiPlanUForTest()), 0.15f) << "ball " << i;
    }
}

TEST(AutoPlay, CollectsACapsuleItCanReachInTime) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);    // the ball is on its way up: time to spare
    g.spawnTestPowerUp(0.25f, 0.45f, (int)Game::PU_LIFE);
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasTargetForTest());
    EXPECT_NEAR(g.aiTargetXForTest(), 0.25f, 0.02f);
    step(g, 1.0f);
    EXPECT_EQ(g.lives(), 6);
}

TEST(AutoPlay, LeavesACapsuleTheBallDoesNotAllow) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    // The ball lands on the left in half a second; a capsule on the far
    // right would cost the ball. Catch the ball.
    g.setBallForTest(-0.30f, 0.30f, 0.0f, 1.0f);
    g.spawnTestPowerUp(0.33f, 0.50f, (int)Game::PU_WIDE);
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasTargetForTest());
    EXPECT_LT(g.aiTargetXForTest(), 0.0f);
    step(g, 1.0f);
    EXPECT_EQ(g.lives(), 5);
}

TEST(AutoPlay, SkipsAWideCapsuleWhileWideWithTimeLeft) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    g.activatePowerUpForTest((int)Game::PU_WIDE);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);
    g.spawnTestPowerUp(0.25f, 0.45f, (int)Game::PU_WIDE);
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasTargetForTest());
    EXPECT_GT(fabsf(g.aiTargetXForTest() - 0.25f), 0.05f);
    // An extra life in the same spot is always worth it.
    Game h;
    startPlaying(h);
    h.setAutoPlayForTest(true);
    h.activatePowerUpForTest((int)Game::PU_WIDE);
    h.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);
    h.spawnTestPowerUp(0.25f, 0.45f, (int)Game::PU_LIFE);
    h.update(1.0f / 60.0f);
    ASSERT_TRUE(h.aiHasTargetForTest());
    EXPECT_NEAR(h.aiTargetXForTest(), 0.25f, 0.02f);
}

TEST(AutoPlay, SavesTheBallThatComesDownFirst) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    g.setBallForTest(-0.30f, 0.40f, 0.0f, 1.0f);   // this one lands in a quarter second
    g.addBallForTest(0.30f, -0.60f, 0.0f, 1.0f);   // this one takes over a second
    g.update(1.0f / 60.0f);
    ASSERT_TRUE(g.aiHasTargetForTest());
    EXPECT_LT(g.aiTargetXForTest(), 0.0f);
}

TEST(AutoPlay, ReplansWhenTheBallBounces) {
    Game g;
    startPlaying(g);
    g.setAutoPlayForTest(true);
    g.setBallForTest(0.30f, 0.30f, 1.0f, 0.3f);    // about to hit the right wall
    g.update(1.0f / 60.0f);
    float before = g.aiLandingXForTest();
    step(g, 0.5f);                                 // bounced: new direction, new plan
    ASSERT_TRUE(g.aiHasLandingForTest());
    EXPECT_NEAR(g.aiLandingXForTest(), before, 0.05f);   // same landing, re-derived
    EXPECT_LT(g.ballDxForTest(0), 0.0f);
}

TEST(AutoPlay, ClearsLevelOneWithoutLosingABall) {
    for (uint32_t seed = 1; seed <= 4; seed++) {
        Game g;
        g.setViewport(kW, kH);
        g.seedRng(seed * 0x9E3779B9u);
        g.triggerNewGameForTest();
        g.setAutoPlayForTest(true);
        for (int i = 0; i < 60 * 180 && g.isPlayingForTest(); i++)
            g.update(1.0f / 60.0f);
        EXPECT_TRUE(g.isLevelClearForTest()) << "seed " << seed << ": bricks left "
                                              << g.breakableLeftForTest();
        EXPECT_EQ(g.lives(), 5) << "seed " << seed;
    }
}

TEST(AutoPlay, HandlesMultiBall) {
    Game g;
    g.setViewport(kW, kH);
    g.startLevelForTest(3);
    g.setAutoPlayForTest(true);
    step(g, 1.0f);                                 // launched
    g.activatePowerUpForTest((int)Game::PU_MULTI);
    ASSERT_EQ(g.ballCount(), 3);
    int lives = g.lives();
    for (int i = 0; i < 60 * 20 && g.isPlayingForTest(); i++) g.update(1.0f / 60.0f);
    EXPECT_GE(g.lives(), lives);                   // three balls up, none of them cost a life
}

TEST(AutoPlay, PlaysTheLaneOnAWideScreen) {
    Game g;
    g.setViewport(2400, 1080);
    g.seedRng(7u);
    g.triggerNewGameForTest();
    g.setAutoPlayForTest(true);
    for (int i = 0; i < 60 * 240 && g.isPlayingForTest(); i++) g.update(1.0f / 60.0f);
    EXPECT_TRUE(g.isLevelClearForTest()) << "bricks left " << g.breakableLeftForTest();
}

// ── AI Glasses plumbing ──────────────────────────────────────────────────────

TEST(Glasses, TouchbarSteersFromAnywhere) {
    Game g;
    startPlaying(g);
    g.setControlMode(Game::CONTROL_TOUCHBAR);
    // Mid-screen touch — ignored by the phone strip, but the whole surface is
    // the touchbar on the glasses.
    g.onPointerDown(0, pxOf(0.25f), kH * 0.4f);
    step(g, 0.4f);
    EXPECT_GT(g.paddleX(), 0.05f);
}

TEST(Glasses, TouchbarContactLaunchesTheBall) {
    Game g;
    startPlaying(g);
    g.setControlMode(Game::CONTROL_TOUCHBAR);
    step(g, 0.6f);
    g.onPointerDown(0, pxOf(0.0f), kH * 0.4f);
    g.update(1.0f / 60.0f);
    EXPECT_FALSE(g.ballHeldForTest());
}

TEST(Glasses, StripModeStillIgnoresMidScreenTouch) {
    Game g;
    startPlaying(g);
    g.onPointerDown(0, pxOf(0.25f), kH * 0.4f);
    step(g, 0.4f);
    EXPECT_FLOAT_EQ(g.paddleX(), 0.0f);
}

TEST(Glasses, SettingsRowLaunchesWhenConnected) {
    Game g;
    g.setViewport(kW, kH);
    bool launched = false;
    g.setGlassesLaunchCallback([&]() { launched = true; return true; });
    g.setGlassesConnected(true);
    openSettings(g);
    ASSERT_TRUE(g.inSettingsForTest());
    tap(g, pxOf(0.0f), pyOf(0.35f));                 // glasses row
    g.update(0.016f);
    EXPECT_TRUE(launched);
}

TEST(Glasses, SettingsRowInertWhenNotConnected) {
    Game g;
    g.setViewport(kW, kH);
    bool launched = false;
    g.setGlassesLaunchCallback([&]() { launched = true; return true; });
    g.setGlassesConnected(false);
    openSettings(g);
    tap(g, pxOf(0.0f), pyOf(0.35f));
    g.update(0.016f);
    EXPECT_FALSE(launched);
    EXPECT_TRUE(g.inSettingsForTest());              // stayed put, no crash
}

TEST(Glasses, SettingsRowExitsWhenActive) {
    Game g;
    g.setViewport(kW, kH);
    bool exited = false;
    g.setGlassesExitCallback([&]() { exited = true; });
    g.setGlassesConnected(true);
    g.setGlassesActive(true);
    openSettings(g);
    tap(g, pxOf(0.0f), pyOf(0.35f));
    g.update(0.016f);
    EXPECT_TRUE(exited);
}

TEST(Glasses, PhoneGameplayFreezesWhileOnGlasses) {
    Game g;
    startPlaying(g);
    g.setBallForTest(0.0f, 0.0f, 0.0f, -1.0f);
    g.spawnTestPowerUp(0.3f, 0.0f, (int)Game::PU_WIDE);
    g.setGlassesActive(true);
    float y = g.ballYForTest(0);
    long score0 = g.score();
    step(g, 1.0f);
    EXPECT_FLOAT_EQ(g.ballYForTest(0), y);           // ball frozen mid-air
    EXPECT_EQ(g.powerUpCountForTest(), 1);           // capsule frozen too
    EXPECT_EQ(g.score(), score0);
    // Hand the game back: everything moves again.
    g.setGlassesActive(false);
    step(g, 1.0f);
    EXPECT_NE(g.ballYForTest(0), y);
}

TEST(Glasses, PhoneBannerDrawsOnlyWhileActive) {
    Game idle;   startPlaying(idle);
    Game active; startPlaying(active);
    active.setGlassesActive(true);
    std::vector<DrawCmd> a, b;
    idle.render(a);
    active.render(b);
    EXPECT_GT(b.size(), a.size());                   // the ON GLASSES banner
}

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

// ── Title demo ───────────────────────────────────────────────────────────────

TEST(Title, DemoBallBouncesInsideTheSideWalls) {
    Game g;
    g.setViewport(kW, kH);
    for (int i = 0; i < 60 * 20; i++) {
        g.update(1.0f / 60.0f);
        ASSERT_LE(fabsf(g.demoBallXForTest()), aspect());
        ASSERT_LT(g.demoBallYForTest(), kPaddleY);   // never below the paddle line
    }
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
    tap(g, pxOf(0.0f), pyOf(0.66f));
    g.update(0.016f);

    g.triggerNewGameForTest();
    g.activatePowerUpForTest((int)Game::PU_WIDE);
    g.activatePowerUpForTest((int)Game::PU_SLOW);
    g.spawnTestPowerUp(0.2f, 0.0f, (int)Game::PU_LIFE);
    g.spawnTestPowerUp(-0.2f, 0.0f, (int)Game::PU_MULTI);
    step(g, 0.6f);
    g.launchForTest();
    step(g, 0.5f);
    cmds.clear(); g.render(cmds);                      // PLAYING (HUD, wall, ball, capsules)
    EXPECT_GT(cmds.size(), 0u) << "PLAYING drew nothing";

    g.startLevelForTest(10);                           // the densest wall
    cmds.clear(); g.render(cmds);
    EXPECT_GT(cmds.size(), 0u) << "LEVEL 10 drew nothing";
    for (const auto& c : cmds) {
        float ext = fabsf(c.mtx[0]) + fabsf(c.mtx[1]);   // NDC half-width
        EXPECT_LE(fabsf(c.tx) + ext, 1.05f) << "shape past the screen edge at tx=" << c.tx;
    }

    g.clearWallForTest();
    g.update(0.016f);                                  // WIN
    ASSERT_TRUE(g.isWinForTest());
    step(g, 0.5f);
    cmds.clear(); g.render(cmds);
    EXPECT_GT(cmds.size(), 0u) << "WIN drew nothing";

    Game h;
    startPlaying(h);
    h.clearWallForTest();
    h.update(0.016f);                                  // LEVEL_CLEAR
    ASSERT_TRUE(h.isLevelClearForTest());
    cmds.clear(); h.render(cmds);
    EXPECT_GT(cmds.size(), 0u) << "LEVEL_CLEAR drew nothing";
    step(h, 2.0f);
    h.setLivesForTest(1);
    loseABall(h);                                      // GAME_OVER
    ASSERT_TRUE(h.isGameOverForTest());
    step(h, 0.5f);
    cmds.clear(); h.render(cmds);
    EXPECT_GT(cmds.size(), 0u) << "GAME_OVER drew nothing";
}

// The end-screen score must shrink to fit: at the fixed 0.30 glyph height,
// four digits already touch the sides of a portrait phone and six run far
// past the edges. Every quad must stay inside NDC once the dust has settled.
TEST(Render, EndScreenScoreFitsTheScreen) {
    Game g;
    startPlaying(g);
    gameOverAtScore(g, 123456);                    // six digits
    step(g, 1.0f);                                 // let shake and debris settle
    std::vector<DrawCmd> cmds;
    g.render(cmds);
    ASSERT_GT(cmds.size(), 0u);
    for (const auto& c : cmds) {
        if (c.shape != SHAPE_QUAD) continue;
        float ext = fabsf(c.mtx[0]) + fabsf(c.mtx[1]);   // NDC half-width
        EXPECT_LE(fabsf(c.tx) + ext, 1.05f)
            << "quad past the screen edge at tx=" << c.tx;
    }
}

TEST(Render, SettingsRowsDrawInEveryGlassesState) {
    for (int mode = 0; mode < 3; mode++) {
        Game g;
        g.setViewport(kW, kH);
        if (mode >= 1) g.setGlassesConnected(true);
        if (mode == 2) g.setGlassesActive(true);
        openSettings(g);
        ASSERT_TRUE(g.inSettingsForTest());
        std::vector<DrawCmd> cmds;
        g.render(cmds);
        EXPECT_GT(cmds.size(), 0u);
        for (const auto& c : cmds) {
            float ext = fabsf(c.mtx[0]) + fabsf(c.mtx[1]);
            EXPECT_LE(fabsf(c.tx) + ext, 1.05f) << "mode " << mode << " shape past the edge";
        }
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

// ── High-score file robustness ────────────────────────────────────────────────
// The loader must ignore a file it cannot trust and keep the in-memory table.

static void writeBytes(const char* path, const void* data, size_t n) {
    FILE* f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);
    ASSERT_EQ(fwrite(data, 1, n, f), n);
    fclose(f);
}

TEST(HighScores, FileWithWrongMagicIsIgnored) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    // A full-size record with a foreign magic and plausible entries.
    unsigned char buf[8 + 16 * Game::kMaxScores] = {};
    uint32_t magic = 0x454B4146u, count = Game::kMaxScores;   // "FAKE"
    memcpy(buf, &magic, 4); memcpy(buf + 4, &count, 4);
    for (int i = 0; i < Game::kMaxScores; i++) { int64_t s = 1000 * (i + 1); memcpy(buf + 8 + 16 * i, &s, 8); }
    writeBytes(path, buf, sizeof(buf));

    Game g; g.setViewport(kW, kH); g.setDataPath(dir);
    for (int i = 0; i < Game::kMaxScores; i++) EXPECT_EQ(g.highScoreForTest(i), 0) << "entry " << i;
    remove(path);
}

TEST(HighScores, TruncatedFileIsIgnored) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    // The right magic but only part of the header: the record read fails.
    const unsigned char buf[5] = {0x42, 0x52, 0x4B, 0x4F, 0x01};
    writeBytes(path, buf, sizeof(buf));

    Game g; g.setViewport(kW, kH); g.setDataPath(dir);
    for (int i = 0; i < Game::kMaxScores; i++) EXPECT_EQ(g.highScoreForTest(i), 0) << "entry " << i;
    remove(path);
}

TEST(HighScores, OversizedCountIsClampedToTheTable) {
    const char* dir = scratchDir();
    char path[512];
    snprintf(path, sizeof(path), "%s/highscores.bin", dir);
    // A valid record whose count claims far more entries than the table holds.
    unsigned char buf[8 + 16 * Game::kMaxScores] = {};
    uint32_t magic = 0x4F4B5242u, count = 1000;   // "BRKO"
    memcpy(buf, &magic, 4); memcpy(buf + 4, &count, 4);
    for (int i = 0; i < Game::kMaxScores; i++) {
        int64_t s = 500 - 100 * i; int32_t lvl = i + 1;
        memcpy(buf + 8 + 16 * i, &s, 8); memcpy(buf + 16 + 16 * i, &lvl, 4);
    }
    writeBytes(path, buf, sizeof(buf));

    Game g; g.setViewport(kW, kH); g.setDataPath(dir);
    for (int i = 0; i < Game::kMaxScores; i++) EXPECT_EQ(g.highScoreForTest(i), 500 - 100 * i) << "entry " << i;
    remove(path);
}
