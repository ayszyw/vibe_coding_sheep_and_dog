#pragma once

#include "raylib.h"

#include <string>
#include <vector>

enum class GameState {
    Playing,
    Paused,
    Victory,
};

struct Sheep {
    Vector2 position{};
    Vector2 velocity{};
    Vector2 penTarget{};
    float panic = 0.0f;
    float seed = 0.0f;
    float deliveryTime = -1.0f;
    bool delivered = false;
};

struct FloatingText {
    std::string text;
    Vector2 position{};
    float life = 0.0f;
    Color color = WHITE;
};

struct GrassBlade {
    Vector2 position{};
    float height = 0.0f;
    float lean = 0.0f;
};

class SheepdogGame {
public:
    explicit SheepdogGame(bool smokeTest);
    ~SheepdogGame();

    SheepdogGame(const SheepdogGame&) = delete;
    SheepdogGame& operator=(const SheepdogGame&) = delete;

    void Run();

private:
    void Reset();
    void HandleInput();
    void Update(float dt);
    void UpdatePlaying(float dt);
    void UpdateFloatingTexts(float dt);
    void DeliverSheep(int index);

    void Draw();
    void DrawWorld();
    void DrawField();
    void DrawFence();
    void DrawDog();
    void DrawSheep(const Sheep& sheep);
    void DrawHud();
    void DrawOverlay();
    void DrawFenceSegment(Vector2 start, Vector2 end, Color base, Color highlight);
    void DrawShadow(Vector2 center, float width, float height);

    void LoadGameAssets();
    void UnloadGameAssets();
    void PrepareAudio();

    RenderTexture2D canvas_{};
    Texture2D shadowTexture_{};
    Texture2D dogTexture_{};
    Texture2D sheepTexture_{};
    Texture2D fenceTexture_{};
    GameState state_ = GameState::Playing;
    bool smokeTest_ = false;

    std::vector<Sheep> sheep_;
    std::vector<Vector2> penSlots_;
    std::vector<GrassBlade> grass_;
    std::vector<Vector2> flowers_;
    std::vector<FloatingText> texts_;

    Vector2 dogPosition_{};
    Vector2 dogVelocity_{};
    float dogAngle_ = -PI / 2.0f;
    float dogWalkTime_ = 0.0f;
    float barkTimer_ = 99.0f;
    float barkRadius_ = 0.0f;
    float elapsed_ = 0.0f;
    float victoryTimer_ = 0.0f;

    int delivered_ = 0;
    int score_ = 0;
    int timeBonus_ = 0;

    Sound barkSound_{};
    Sound penSound_{};
    Sound winSound_{};
    bool audioReady_ = false;
};
