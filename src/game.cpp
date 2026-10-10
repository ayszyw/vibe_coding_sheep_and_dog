#include "game.h"

#include "raymath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <utility>

namespace {

constexpr int kLogicalWidth = 720;
constexpr int kLogicalHeight = 1280;

constexpr float kFieldLeft = 50.0f;
constexpr float kFieldRight = 670.0f;
constexpr float kFieldTop = 300.0f;
constexpr float kFieldBottom = 1160.0f;

constexpr float kPenLeft = 180.0f;
constexpr float kPenRight = 540.0f;
constexpr float kPenTop = 120.0f;
constexpr float kPenBottom = 260.0f;

constexpr float kGateLeft = 310.0f;
constexpr float kGateRight = 410.0f;

constexpr int kSheepCount = 60;
constexpr float kSheepRadius = 11.0f;
constexpr float kDogRadius = 20.0f;
constexpr float kDogSpeed = 330.0f;
constexpr float kDogSprintSpeed = 450.0f;
constexpr float kSheepCalmSpeed = 72.0f;
constexpr float kSheepPanicSpeed = 252.0f;
constexpr float kSheepAcceleration = 620.0f;
constexpr float kDogInfluenceRadius = 285.0f;

constexpr Color kBackdrop = {29, 48, 37, 255};
constexpr Color kFieldGrass = {92, 139, 84, 255};
constexpr Color kFieldGrassLight = {103, 151, 93, 255};
constexpr Color kGrassBlade = {46, 91, 47, 255};
constexpr Color kPenFloor = {207, 168, 112, 255};
constexpr Color kPenFloorLight = {220, 182, 125, 255};
constexpr Color kFence = {105, 72, 46, 255};
constexpr Color kFenceLight = {144, 105, 69, 255};
constexpr Color kFencePost = {68, 44, 31, 255};
constexpr Color kGateGlow = {255, 202, 77, 255};
constexpr Color kSheepWool = {247, 243, 230, 255};
constexpr Color kSheepFace = {52, 42, 36, 255};
constexpr Color kDogBody = {177, 94, 53, 255};
constexpr Color kDogLight = {226, 166, 104, 255};
constexpr Color kDogDark = {91, 49, 33, 255};
constexpr Color kPanel = {18, 29, 25, 205};
constexpr Color kPanelBorder = {255, 255, 255, 32};
constexpr Color kTextMuted = {222, 231, 218, 190};


Vector2 LerpVector(Vector2 from, Vector2 to, float amount) {
    return {
        from.x + (to.x - from.x) * amount,
        from.y + (to.y - from.y) * amount,
    };
}

Vector2 MoveTowardsVector(Vector2 value, Vector2 target, float maxDistance) {
    const float dx = target.x - value.x;
    const float dy = target.y - value.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= maxDistance || distance <= 0.0001f) {
        return target;
    }
    const float scale = maxDistance / distance;
    return {value.x + dx * scale, value.y + dy * scale};
}

float BoundaryForce(float distanceToWall, float margin, float strength) {
    if (distanceToWall <= 0.0f || distanceToWall >= margin) {
        return 0.0f;
    }
    const float closeness = 1.0f - distanceToWall / margin;
    return strength * closeness * closeness;
}

bool IsInGate(float x) {
    return x >= kGateLeft && x <= kGateRight;
}

Wave MakeTone(float frequency, float duration, float amplitude) {
    constexpr int sampleRate = 44100;
    const int frameCount = static_cast<int>(duration * static_cast<float>(sampleRate));
    auto* samples = static_cast<short*>(MemAlloc(frameCount * sizeof(short)));
    for (int i = 0; i < frameCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float envelope = std::pow(1.0f - t / duration, 2.0f);
        const float value = std::sin(2.0f * PI * frequency * t) * envelope;
        samples[i] = static_cast<short>(value * amplitude * 32767.0f);
    }

    Wave wave{};
    wave.frameCount = frameCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples;
    return wave;
}

}  // namespace

SheepdogGame::SheepdogGame(bool smokeTest) : smokeTest_(smokeTest) {}

SheepdogGame::~SheepdogGame() = default;

void SheepdogGame::Run() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(kLogicalWidth, kLogicalHeight, "Sheepdog - Top-Down Herding Demo");
    SetWindowMinSize(450, 800);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    canvas_ = LoadRenderTexture(kLogicalWidth, kLogicalHeight);
    LoadGameAssets();
    PrepareAudio();
    Reset();

    const double startTime = GetTime();
    while (!WindowShouldClose()) {
        const float dt = Clamp(GetFrameTime(), 0.0f, 0.05f);
        HandleInput();
        Update(dt);
        Draw();

        if (smokeTest_ && GetTime() - startTime > 1.5) {
            TakeScreenshot("smoke-screenshot.png");
            break;
        }
    }

    UnloadRenderTexture(canvas_);
    UnloadGameAssets();
    if (audioReady_) {
        UnloadSound(barkSound_);
        UnloadSound(penSound_);
        UnloadSound(winSound_);
    }
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    CloseWindow();
}

void SheepdogGame::Reset() {
    state_ = GameState::Playing;
    sheep_.clear();
    sheep_.reserve(kSheepCount);
    texts_.clear();
    dogPosition_ = {160.0f, 860.0f};
    dogVelocity_ = {0.0f, 0.0f};
    dogAngle_ = -PI / 2.0f;
    dogWalkTime_ = 0.0f;
    barkTimer_ = 99.0f;
    barkRadius_ = 0.0f;
    elapsed_ = 0.0f;
    victoryTimer_ = 0.0f;
    delivered_ = 0;
    score_ = 0;
    timeBonus_ = 0;

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const float spawnMinX = kFieldLeft + 70.0f;
    const float spawnMaxX = kFieldRight - 70.0f;
    const float spawnMinY = kFieldTop + 70.0f;
    const float spawnMaxY = kFieldBottom - 70.0f;

    for (int i = 0; i < kSheepCount; ++i) {
        Vector2 position{};
        bool valid = false;
        for (int attempt = 0; attempt < 220 && !valid; ++attempt) {
            position = {
                spawnMinX + unit(rng) * (spawnMaxX - spawnMinX),
                spawnMinY + unit(rng) * (spawnMaxY - spawnMinY),
            };
            valid = Vector2Distance(position, dogPosition_) > 230.0f;
            if (valid) {
                for (const Sheep& sheep : sheep_) {
                    if (Vector2Distance(position, sheep.position) < 42.0f) {
                        valid = false;
                        break;
                    }
                }
            }
        }

        Sheep sheep;
        sheep.position = position;
        sheep.seed = unit(rng) * 2.0f * PI;
        sheep_.push_back(sheep);
    }

    const int columns = 12;
    const int rows = (kSheepCount + columns - 1) / columns;
    const float slotLeft = kPenLeft + 34.0f;
    const float slotRight = kPenRight - 34.0f;
    const float slotTop = kPenTop + 18.0f;
    const float slotBottom = kPenBottom - 14.0f;
    penSlots_.clear();
    penSlots_.reserve(kSheepCount);
    for (int i = 0; i < kSheepCount; ++i) {
        const int column = i % columns;
        const int row = i / columns;
        const float x = slotLeft + static_cast<float>(column) / static_cast<float>(columns - 1) * (slotRight - slotLeft);
        const float y = rows == 1 ? (slotTop + slotBottom) * 0.5f
                                  : slotTop + static_cast<float>(row) / static_cast<float>(rows - 1) * (slotBottom - slotTop);
        penSlots_.push_back({x, y});
    }

    grass_.clear();
    flowers_.clear();
    for (int i = 0; i < 220; ++i) {
        GrassBlade blade;
        blade.position = {
            kFieldLeft + 16.0f + unit(rng) * (kFieldRight - kFieldLeft - 32.0f),
            kFieldTop + 16.0f + unit(rng) * (kFieldBottom - kFieldTop - 32.0f),
        };
        blade.height = 4.0f + unit(rng) * 5.0f;
        blade.lean = -3.0f + unit(rng) * 6.0f;
        grass_.push_back(blade);
    }
    for (int i = 0; i < 30; ++i) {
        flowers_.push_back({
            kFieldLeft + 28.0f + unit(rng) * (kFieldRight - kFieldLeft - 56.0f),
            kFieldTop + 28.0f + unit(rng) * (kFieldBottom - kFieldTop - 56.0f),
        });
    }
}

void SheepdogGame::HandleInput() {
    if (IsKeyPressed(KEY_R)) {
        Reset();
        return;
    }

    if (state_ == GameState::Playing && IsKeyPressed(KEY_P)) {
        state_ = GameState::Paused;
    } else if (state_ == GameState::Paused && (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ENTER))) {
        state_ = GameState::Playing;
    } else if (state_ == GameState::Victory && IsKeyPressed(KEY_ENTER)) {
        Reset();
    }
}

void SheepdogGame::Update(float dt) {
    UpdateFloatingTexts(dt);
    if (barkTimer_ < 2.0f) {
        barkTimer_ += dt;
        barkRadius_ += 620.0f * dt;
    }

    if (state_ == GameState::Playing) {
        UpdatePlaying(dt);
    } else if (state_ == GameState::Victory) {
        victoryTimer_ += dt;
    }
}

void SheepdogGame::UpdatePlaying(float dt) {
    elapsed_ += dt;

    Vector2 input{};
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
        input.x += 1.0f;
    }
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
        input.x -= 1.0f;
    }
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) {
        input.y += 1.0f;
    }
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
        input.y -= 1.0f;
    }

    const float inputLength = Vector2Length(input);
    if (inputLength > 0.0f) {
        input = Vector2Scale(Vector2Normalize(input), 1.0f);
    }

    const bool sprinting = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const float speed = sprinting ? kDogSprintSpeed : kDogSpeed;
    const Vector2 desiredVelocity = Vector2Scale(input, speed);
    dogVelocity_ = LerpVector(dogVelocity_, desiredVelocity, 1.0f - std::exp(-11.0f * dt));
    dogPosition_.x += dogVelocity_.x * dt;
    dogPosition_.y += dogVelocity_.y * dt;

    if (Vector2Length(dogVelocity_) > 12.0f) {
        dogAngle_ = std::atan2(dogVelocity_.y, dogVelocity_.x);
        dogWalkTime_ += dt * (sprinting ? 13.0f : 9.0f);
    }

    const float dogMargin = kDogRadius + 8.0f;
    dogPosition_.x = Clamp(dogPosition_.x, kFieldLeft + dogMargin, kFieldRight - dogMargin);
    dogPosition_.y = Clamp(dogPosition_.y, kFieldTop + dogMargin, kFieldBottom - dogMargin);

    if (IsKeyPressed(KEY_SPACE)) {
        barkTimer_ = 0.0f;
        barkRadius_ = 18.0f;
        if (audioReady_) {
            PlaySound(barkSound_);
        }
        for (Sheep& sheep : sheep_) {
            if (sheep.delivered) {
                continue;
            }
            Vector2 away = Vector2Subtract(sheep.position, dogPosition_);
            const float distance = Vector2Length(away);
            if (distance > 0.0f && distance < 340.0f) {
                away = Vector2Normalize(away);
                sheep.panic = 1.0f;
                sheep.velocity = Vector2Add(sheep.velocity, Vector2Scale(away, 320.0f));
            }
        }
    }

    for (int i = 0; i < static_cast<int>(sheep_.size()); ++i) {
        Sheep& sheep = sheep_[i];
        if (sheep.delivered) {
            sheep.deliveryTime += dt;
            sheep.position = LerpVector(sheep.position, sheep.penTarget, 1.0f - std::exp(-5.0f * dt));
            continue;
        }

        const float wanderPhase = sheep.seed * 1.7f + elapsed_ * 0.55f;
        Vector2 steering = {
            std::cos(wanderPhase) * 20.0f,
            std::sin(sheep.seed * 0.9f + elapsed_ * 0.37f) * 20.0f,
        };

        Vector2 separation{};
        Vector2 alignment{};
        Vector2 cohesion{};
        int nearbyCount = 0;

        for (int j = 0; j < static_cast<int>(sheep_.size()); ++j) {
            if (i == j || sheep_[j].delivered) {
                continue;
            }
            const Vector2 delta = Vector2Subtract(sheep.position, sheep_[j].position);
            const float distance = Vector2Length(delta);
            if (distance <= 0.0001f) {
                continue;
            }

            if (distance < 36.0f) {
                const float weight = (36.0f - distance) / 36.0f;
                separation = Vector2Add(separation, Vector2Scale(Vector2Scale(delta, 1.0f / distance), weight * 82.0f));
            }
            if (distance < 112.0f) {
                alignment = Vector2Add(alignment, Vector2Scale(sheep_[j].velocity, 0.11f));
                cohesion = Vector2Add(cohesion, Vector2Scale(delta, 0.055f));
                ++nearbyCount;
            }
        }
        if (nearbyCount > 0) {
            alignment = Vector2Scale(alignment, 1.0f / static_cast<float>(nearbyCount));
            cohesion = Vector2Scale(cohesion, 1.0f / static_cast<float>(nearbyCount));
        }

        Vector2 dogFlee{};
        const Vector2 fromDog = Vector2Subtract(sheep.position, dogPosition_);
        const float dogDistance = Vector2Length(fromDog);
        float panicTarget = 0.0f;
        if (dogDistance < kDogInfluenceRadius && dogDistance > 0.0001f) {
            const float closeness = 1.0f - dogDistance / kDogInfluenceRadius;
            panicTarget = std::pow(closeness, 1.55f);
            const Vector2 away = Vector2Scale(fromDog, 1.0f / dogDistance);
            dogFlee = Vector2Scale(away, 35.0f + 360.0f * panicTarget);
            sheep.panic = std::max(sheep.panic, panicTarget);
        } else {
            sheep.panic = std::max(0.0f, sheep.panic - 1.6f * dt);
        }

        Vector2 wall{};
        const float margin = 34.0f;
        if (sheep.position.x < kFieldLeft + margin) {
            wall.x += BoundaryForce(sheep.position.x - kFieldLeft, margin, 300.0f);
        }
        if (sheep.position.x > kFieldRight - margin) {
            wall.x -= BoundaryForce(kFieldRight - sheep.position.x, margin, 300.0f);
        }
        if (!IsInGate(sheep.position.x) && sheep.position.y < kFieldTop + margin) {
            wall.y += BoundaryForce(sheep.position.y - kFieldTop, margin, 300.0f);
        }
        if (sheep.position.y > kFieldBottom - margin) {
            wall.y -= BoundaryForce(kFieldBottom - sheep.position.y, margin, 300.0f);
        }

        const float cornerRadius = 105.0f;
        const std::array<Vector2, 4> corners = {{
            {kFieldLeft, kFieldTop},
            {kFieldRight, kFieldTop},
            {kFieldLeft, kFieldBottom},
            {kFieldRight, kFieldBottom},
        }};
        for (const Vector2& corner : corners) {
            const Vector2 fromCorner = Vector2Subtract(sheep.position, corner);
            const float cornerDistance = Vector2Length(fromCorner);
            if (cornerDistance > 0.0f && cornerDistance < cornerRadius) {
                const float closeness = 1.0f - cornerDistance / cornerRadius;
                const float strength = 330.0f * closeness * closeness;
                wall = Vector2Add(wall, Vector2Scale(fromCorner, strength / cornerDistance));
            }
        }

        if (!IsInGate(sheep.position.x) && sheep.position.y < kFieldTop + 130.0f) {
            const float gateCenterX = (kGateLeft + kGateRight) * 0.5f;
            const float closeness = 1.0f - (sheep.position.y - kFieldTop) / 130.0f;
            const float direction = sheep.position.x < gateCenterX ? 1.0f : -1.0f;
            wall.x += direction * 105.0f * closeness * closeness;
        }

        if (Vector2Length(sheep.velocity) < 14.0f && dogDistance < 230.0f) {
            const float unstickAngle = sheep.seed * 17.0f + elapsed_ * 2.4f;
            steering = Vector2Add(steering, {std::cos(unstickAngle) * 95.0f, std::sin(unstickAngle) * 95.0f});
        }

        steering = Vector2Add(steering, Vector2Scale(separation, 1.35f));
        steering = Vector2Add(steering, alignment);
        steering = Vector2Add(steering, cohesion);
        steering = Vector2Add(steering, dogFlee);
        steering = Vector2Add(steering, Vector2Scale(wall, 2.8f));

        const float steeringLength = Vector2Length(steering);
        if (steeringLength > 0.0001f) {
            steering = Vector2Scale(steering, 1.0f / steeringLength);
        }
        const float targetSpeed = kSheepCalmSpeed + (kSheepPanicSpeed - kSheepCalmSpeed) * sheep.panic;
        const Vector2 desiredVelocity = Vector2Scale(steering, targetSpeed);
        sheep.velocity = MoveTowardsVector(sheep.velocity, desiredVelocity, kSheepAcceleration * dt);
        sheep.position = Vector2Add(sheep.position, Vector2Scale(sheep.velocity, dt));

        sheep.position.x = Clamp(sheep.position.x, kFieldLeft + kSheepRadius, kFieldRight - kSheepRadius);
        if (IsInGate(sheep.position.x)) {
            sheep.position.y = std::max(sheep.position.y, kPenTop - 80.0f);
        } else {
            sheep.position.y = Clamp(sheep.position.y, kFieldTop + kSheepRadius, kFieldBottom - kSheepRadius);
        }

        if (IsInGate(sheep.position.x) && sheep.position.y <= kPenBottom + 8.0f) {
            DeliverSheep(i);
        }
    }
}

void SheepdogGame::UpdateFloatingTexts(float dt) {
    for (FloatingText& text : texts_) {
        text.life += dt;
        text.position.y -= 24.0f * dt;
    }
    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(), [](const FloatingText& text) { return text.life > 1.35f; }),
        texts_.end());
}

void SheepdogGame::DeliverSheep(int index) {
    Sheep& sheep = sheep_[index];
    sheep.delivered = true;
    sheep.deliveryTime = 0.0f;
    sheep.velocity = {0.0f, 0.0f};
    sheep.penTarget = penSlots_[delivered_];
    ++delivered_;
    score_ += 100;

    texts_.push_back({"+100", {sheep.position.x, sheep.position.y - 20.0f}, 0.0f, kGateGlow});

    if (audioReady_) {
        PlaySound(penSound_);
    }

    if (delivered_ == kSheepCount) {
        timeBonus_ = std::max(0, static_cast<int>((180.0f - elapsed_) * 15.0f));
        score_ += timeBonus_;
        state_ = GameState::Victory;
        victoryTimer_ = 0.0f;
        if (audioReady_) {
            PlaySound(winSound_);
        }
    }
}

void SheepdogGame::Draw() {
    BeginTextureMode(canvas_);
    DrawWorld();
    DrawHud();
    if (state_ == GameState::Paused || (state_ == GameState::Victory && victoryTimer_ > 0.45f)) {
        DrawOverlay();
    }
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);

    const float scale = std::min(
        static_cast<float>(GetScreenWidth()) / static_cast<float>(kLogicalWidth),
        static_cast<float>(GetScreenHeight()) / static_cast<float>(kLogicalHeight));
    const float drawWidth = static_cast<float>(kLogicalWidth) * scale;
    const float drawHeight = static_cast<float>(kLogicalHeight) * scale;
    const Rectangle destination = {
        (static_cast<float>(GetScreenWidth()) - drawWidth) * 0.5f,
        (static_cast<float>(GetScreenHeight()) - drawHeight) * 0.5f,
        drawWidth,
        drawHeight,
    };
    const Rectangle source = {
        0.0f,
        0.0f,
        static_cast<float>(canvas_.texture.width),
        -static_cast<float>(canvas_.texture.height),
    };
    DrawTexturePro(canvas_.texture, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
    EndDrawing();
}

void SheepdogGame::DrawWorld() {
    ClearBackground(kBackdrop);
    DrawField();
    DrawFence();

    std::vector<int> order;
    order.reserve(kSheepCount);
    for (int i = 0; i < static_cast<int>(sheep_.size()); ++i) {
        if (!sheep_[i].delivered) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [this](int a, int b) { return sheep_[a].position.y < sheep_[b].position.y; });

    for (int i : order) {
        DrawSheep(sheep_[i]);
    }
    for (const Sheep& sheep : sheep_) {
        if (sheep.delivered) {
            DrawSheep(sheep);
        }
    }
    DrawDog();

    if (barkTimer_ < 0.42f) {
        const float alpha = 1.0f - barkTimer_ / 0.42f;
        DrawCircleLinesV(dogPosition_, barkRadius_, Fade(Color{255, 229, 154, 255}, alpha * 0.9f));
        DrawCircleLinesV(dogPosition_, barkRadius_ * 0.72f, Fade(Color{255, 202, 77, 255}, alpha));
    }

    for (const FloatingText& text : texts_) {
        const float alpha = Clamp(1.0f - text.life / 1.35f, 0.0f, 1.0f);
        DrawText(text.text.c_str(), static_cast<int>(text.position.x) - 17, static_cast<int>(text.position.y), 26, Fade(text.color, alpha));
    }
}

void SheepdogGame::DrawField() {
    DrawRectangleRounded({kFieldLeft - 12.0f, kFieldTop - 12.0f, kFieldRight - kFieldLeft + 24.0f, kFieldBottom - kFieldTop + 24.0f}, 0.02f, 12, Color{23, 39, 31, 255});
    DrawRectangle(static_cast<int>(kFieldLeft), static_cast<int>(kFieldTop), static_cast<int>(kFieldRight - kFieldLeft), static_cast<int>(kFieldBottom - kFieldTop), kFieldGrass);

    for (float x = kFieldLeft; x < kFieldRight; x += 84.0f) {
        DrawRectangle(static_cast<int>(x), static_cast<int>(kFieldTop), 2, static_cast<int>(kFieldBottom - kFieldTop), Color{255, 255, 255, 9});
    }
    for (float y = kFieldTop; y < kFieldBottom; y += 84.0f) {
        DrawRectangle(static_cast<int>(kFieldLeft), static_cast<int>(y), static_cast<int>(kFieldRight - kFieldLeft), 2, Color{255, 255, 255, 7});
    }

    for (const GrassBlade& blade : grass_) {
        DrawLineEx(blade.position, {blade.position.x + blade.lean, blade.position.y - blade.height}, 2.0f, kGrassBlade);
    }
    for (const Vector2& flower : flowers_) {
        DrawCircleV(flower, 2.2f, Color{255, 241, 197, 235});
        DrawCircleV({flower.x - 4.0f, flower.y}, 2.5f, Color{255, 255, 255, 175});
        DrawCircleV({flower.x + 4.0f, flower.y}, 2.5f, Color{255, 255, 255, 175});
        DrawCircleV({flower.x, flower.y - 4.0f}, 2.5f, Color{255, 255, 255, 175});
        DrawCircleV({flower.x, flower.y + 4.0f}, 2.5f, Color{255, 255, 255, 175});
    }

    DrawRectangleRounded({kPenLeft - 12.0f, kPenTop - 12.0f, kPenRight - kPenLeft + 24.0f, kPenBottom - kPenTop + 24.0f}, 0.03f, 10, Color{119, 80, 45, 255});
    DrawRectangle(static_cast<int>(kPenLeft), static_cast<int>(kPenTop), static_cast<int>(kPenRight - kPenLeft), static_cast<int>(kPenBottom - kPenTop), kPenFloor);
    for (float x = kPenLeft; x < kPenRight; x += 62.0f) {
        DrawRectangle(static_cast<int>(x), static_cast<int>(kPenTop), 2, static_cast<int>(kPenBottom - kPenTop), kPenFloorLight);
    }

    const float pulse = 0.12f + 0.08f * std::sin(static_cast<float>(GetTime()) * 4.0f);
    DrawRectangle(static_cast<int>(kGateLeft), static_cast<int>(kPenBottom - 8.0f), static_cast<int>(kGateRight - kGateLeft), 52, Fade(kGateGlow, pulse));

    constexpr int gateMidX = static_cast<int>((kGateLeft + kGateRight) * 0.5f);
    DrawTriangle(
        {static_cast<float>(gateMidX - 16), kPenBottom + 9.0f},
        {static_cast<float>(gateMidX + 16), kPenBottom + 9.0f},
        {static_cast<float>(gateMidX), kPenBottom + 24.0f},
        Fade(kGateGlow, 0.85f));
    DrawText("PEN", gateMidX - 24, 196, 24, Color{73, 48, 27, 255});
    DrawText("DRIVE SHEEP TO PEN", gateMidX - MeasureText("DRIVE SHEEP TO PEN", 17) / 2, 318, 17, Color{255, 255, 255, 165});
}

void SheepdogGame::DrawFence() {
    const Color base = kFence;
    const Color light = kFenceLight;

    DrawFenceSegment({kFieldLeft, kFieldTop}, {kGateLeft, kFieldTop}, base, light);
    DrawFenceSegment({kGateRight, kFieldTop}, {kFieldRight, kFieldTop}, base, light);
    DrawFenceSegment({kFieldRight, kFieldTop}, {kFieldRight, kFieldBottom}, base, light);
    DrawFenceSegment({kFieldRight, kFieldBottom}, {kFieldLeft, kFieldBottom}, base, light);
    DrawFenceSegment({kFieldLeft, kFieldBottom}, {kFieldLeft, kFieldTop}, base, light);

    DrawFenceSegment({kPenLeft, kPenTop}, {kPenRight, kPenTop}, base, light);
    DrawFenceSegment({kPenRight, kPenTop}, {kPenRight, kFieldTop}, base, light);
    DrawFenceSegment({kPenRight, kPenBottom}, {kGateRight, kPenBottom}, base, light);
    DrawFenceSegment({kGateLeft, kPenBottom}, {kPenLeft, kPenBottom}, base, light);
    DrawFenceSegment({kPenLeft, kPenBottom}, {kPenLeft, kFieldTop}, base, light);

    DrawLineEx({kGateLeft, kPenBottom}, {kGateLeft, kFieldTop}, 12.0f, kFencePost);
    DrawLineEx({kGateRight, kPenBottom}, {kGateRight, kFieldTop}, 12.0f, kFencePost);
    DrawCircleV({kGateLeft, kPenBottom - 2.0f}, 9.0f, kGateGlow);
    DrawCircleV({kGateRight, kPenBottom - 2.0f}, 9.0f, kGateGlow);
}

void SheepdogGame::DrawDog() {
    const Vector2 forward = {std::cos(dogAngle_), std::sin(dogAngle_)};
    const Vector2 side = {-forward.y, forward.x};
    const float bob = std::sin(dogWalkTime_ * 2.0f) * 1.2f;

    DrawEllipse(dogPosition_.x, dogPosition_.y + 6.0f, 24.0f, 10.0f, Color{0, 0, 0, 45});
    if (dogTexture_.id != 0) {
        const Vector2 position = {dogPosition_.x, dogPosition_.y + bob * 0.35f};
        const Rectangle source = {0.0f, 0.0f, 96.0f, 96.0f};
        const Rectangle destination = {position.x - 34.0f, position.y - 34.0f, 68.0f, 68.0f};
        DrawTexturePro(dogTexture_, source, destination, {34.0f, 34.0f}, dogAngle_ * RAD2DEG + 90.0f, WHITE);

        if (barkTimer_ < 0.32f) {
            const Vector2 snout = Vector2Add(position, Vector2Scale(forward, 25.0f));
            DrawCircleV(Vector2Add(snout, Vector2Scale(forward, 3.0f)), 4.2f, Color{97, 44, 34, 255});
        }
        return;
    }
    DrawEllipse(dogPosition_.x, dogPosition_.y + bob * 0.35f, 23.0f, 19.0f, kDogBody);
    DrawCircleV(Vector2Add(dogPosition_, Vector2Scale(forward, 3.0f)), 18.0f, kDogLight);

    const Vector2 head = Vector2Add(dogPosition_, Vector2Scale(forward, 15.0f));
    DrawCircleV(head, 15.0f, kDogLight);
    DrawTriangle(
        Vector2Add(head, Vector2Add(Vector2Scale(side, 14.0f), Vector2Scale(forward, -2.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, 20.0f), Vector2Scale(forward, -7.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, 8.0f), Vector2Scale(forward, -13.0f))),
        kDogDark);
    DrawTriangle(
        Vector2Add(head, Vector2Add(Vector2Scale(side, -14.0f), Vector2Scale(forward, -2.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, -20.0f), Vector2Scale(forward, -7.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, -8.0f), Vector2Scale(forward, -13.0f))),
        kDogDark);

    const Vector2 snout = Vector2Add(dogPosition_, Vector2Scale(forward, 28.0f));
    DrawCircleV(snout, 9.0f, Color{243, 213, 171, 255});
    DrawCircleV(Vector2Add(snout, Vector2Scale(forward, 5.0f)), 3.5f, kDogDark);

    if (barkTimer_ < 0.32f) {
        DrawCircleV(Vector2Add(snout, Vector2Scale(forward, 4.0f)), 4.2f, Color{97, 44, 34, 255});
    }

    const Vector2 eyeBase = Vector2Add(head, Vector2Scale(forward, 4.0f));
    DrawCircleV(Vector2Add(eyeBase, Vector2Scale(side, 6.0f)), 2.6f, kDogDark);
    DrawCircleV(Vector2Add(eyeBase, Vector2Scale(side, -6.0f)), 2.6f, kDogDark);

    const Vector2 tailBase = Vector2Add(dogPosition_, Vector2Add(Vector2Scale(forward, -21.0f), Vector2Scale(side, 3.0f)));
    const float wag = std::sin(static_cast<float>(GetTime()) * 11.0f) * 0.45f;
    const Vector2 tailTip = Vector2Add(tailBase, Vector2Add(Vector2Scale(forward, -13.0f), Vector2Scale(side, 8.0f + wag * 9.0f)));
    DrawLineEx(tailBase, tailTip, 5.0f, kDogDark);
    DrawCircleV(tailTip, 4.0f, kDogDark);
}

void SheepdogGame::DrawSheep(const Sheep& sheep) {
    float heading = sheep.seed;
    if (Vector2Length(sheep.velocity) > 7.0f) {
        heading = std::atan2(sheep.velocity.y, sheep.velocity.x);
    }
    const Vector2 forward = {std::cos(heading), std::sin(heading)};
    const Vector2 side = {-forward.y, forward.x};
    const float bob = std::sin(static_cast<float>(GetTime()) * 5.0f + sheep.seed) * 0.8f;
    const Vector2 position = {sheep.position.x, sheep.position.y + bob * 0.25f};

    DrawEllipse(position.x, position.y + 4.0f, 15.0f, 7.0f, Color{0, 0, 0, 42});

    if (sheepTexture_.id != 0) {
        const Rectangle source = {0.0f, 0.0f, 64.0f, 64.0f};
        const Rectangle destination = {position.x - 18.0f, position.y - 18.0f, 36.0f, 36.0f};
        DrawTexturePro(sheepTexture_, source, destination, {18.0f, 18.0f}, heading * RAD2DEG + 90.0f, WHITE);
        return;
    }

    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) / 8.0f * 2.0f * PI;
        const Vector2 bump = {std::cos(angle) * 10.0f, std::sin(angle) * 10.0f};
        DrawCircleV(Vector2Add(position, bump), 6.2f, kSheepWool);
    }
    DrawCircleV(position, 14.0f, kSheepWool);

    const Vector2 head = Vector2Add(position, Vector2Scale(forward, 16.0f));
    DrawCircleV(head, 8.0f, kSheepFace);
    DrawTriangle(
        Vector2Add(head, Vector2Add(Vector2Scale(side, 7.0f), Vector2Scale(forward, -2.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, 11.0f), Vector2Scale(forward, 3.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, 3.0f), Vector2Scale(forward, -5.0f))),
        kSheepFace);
    DrawTriangle(
        Vector2Add(head, Vector2Add(Vector2Scale(side, -7.0f), Vector2Scale(forward, -2.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, -11.0f), Vector2Scale(forward, 3.0f))),
        Vector2Add(head, Vector2Add(Vector2Scale(side, -3.0f), Vector2Scale(forward, -5.0f))),
        kSheepFace);
    DrawCircleV(Vector2Add(head, Vector2Scale(forward, 4.0f)), 1.7f, Color{255, 255, 255, 225});
    DrawCircleV(Vector2Add(head, Vector2Scale(forward, 7.0f)), 2.3f, Color{225, 156, 138, 255});
}

void SheepdogGame::DrawHud() {
    DrawRectangleRounded({20.0f, 20.0f, 220.0f, 70.0f}, 0.16f, 8, kPanel);
    DrawRectangleRoundedLinesEx({20.0f, 20.0f, 220.0f, 70.0f}, 0.16f, 8, 1.5f, kPanelBorder);
    DrawText("SHEEPDOG", 36, 28, 23, WHITE);
    DrawText(TextFormat("SHEEP  %02d / %02d", delivered_, kSheepCount), 36, 56, 14, kTextMuted);

    DrawRectangleRounded({480.0f, 20.0f, 220.0f, 70.0f}, 0.14f, 8, kPanel);
    DrawRectangleRoundedLinesEx({480.0f, 20.0f, 220.0f, 70.0f}, 0.14f, 8, 1.5f, kPanelBorder);
    DrawText("SCORE", 496, 28, 12, kTextMuted);
    DrawText(TextFormat("%d", score_), 496, 41, 26, WHITE);

    const int totalSeconds = std::max(0, static_cast<int>(elapsed_));
    DrawText("TIME", 606, 28, 12, kTextMuted);
    DrawText(TextFormat("%02d:%02d", totalSeconds / 60, totalSeconds % 60), 606, 41, 19, WHITE);

    const float progress = static_cast<float>(delivered_) / static_cast<float>(kSheepCount);
    DrawRectangle(20, 100, 680, 8, Color{255, 255, 255, 22});
    DrawRectangleRounded({20.0f, 99.0f, 680.0f * progress, 10.0f}, 0.5f, 4, kGateGlow);

    constexpr const char* hint = "WASD / ARROWS MOVE    SHIFT SPRINT    SPACE BARK    P PAUSE";
    const int hintWidth = MeasureText(hint, 14);
    DrawRectangleRounded({static_cast<float>((kLogicalWidth - hintWidth) / 2 - 18), 1238.0f, static_cast<float>(hintWidth + 36), 25.0f}, 0.5f, 6, Color{18, 29, 25, 155});
    DrawText(hint, (kLogicalWidth - hintWidth) / 2, 1243, 14, Color{230, 236, 226, 185});
}

void SheepdogGame::DrawOverlay() {
    DrawRectangle(0, 0, kLogicalWidth, kLogicalHeight, Fade(Color{6, 13, 10, 255}, 0.68f));

    if (state_ == GameState::Paused) {
        DrawRectangleRounded({140.0f, 460.0f, 440.0f, 190.0f}, 0.08f, 8, Color{24, 37, 32, 248});
        DrawRectangleRoundedLinesEx({140.0f, 460.0f, 440.0f, 190.0f}, 0.08f, 8, 2.0f, Color{255, 255, 255, 35});
        DrawText("PAUSED", 265, 493, 36, WHITE);
        DrawText("P / ENTER  RESUME", 278, 560, 18, kTextMuted);
        DrawText("R  RESTART", 310, 593, 18, kTextMuted);
        return;
    }

    DrawRectangleRounded({50.0f, 410.0f, 620.0f, 275.0f}, 0.06f, 8, Color{24, 37, 32, 252});
    DrawRectangleRoundedLinesEx({50.0f, 410.0f, 620.0f, 275.0f}, 0.06f, 8, 2.0f, Color{255, 202, 77, 120});
    constexpr const char* title = "ALL SHEEP HOME!";
    DrawText(title, (kLogicalWidth - MeasureText(title, 38)) / 2, 441, 38, kGateGlow);
    DrawText(TextFormat("SCORE  %d", score_), (kLogicalWidth - MeasureText(TextFormat("SCORE  %d", score_), 28)) / 2, 509, 28, WHITE);
    DrawText(TextFormat("TIME BONUS  +%d", timeBonus_), (kLogicalWidth - MeasureText(TextFormat("TIME BONUS  +%d", timeBonus_), 17)) / 2, 555, 17, Color{174, 222, 172, 220});
    DrawText("R / ENTER  PLAY AGAIN", (kLogicalWidth - MeasureText("R / ENTER  PLAY AGAIN", 18)) / 2, 617, 18, kTextMuted);
}

void SheepdogGame::DrawFenceSegment(Vector2 start, Vector2 end, Color base, Color highlight) {
    const Vector2 direction = Vector2Normalize(Vector2Subtract(end, start));
    const Vector2 normal = {-direction.y, direction.x};
    if (fenceTexture_.id != 0) {
        const float length = Vector2Distance(start, end);
        const float rotation = std::atan2(direction.y, direction.x) * RAD2DEG;
        float offset = 0.0f;
        while (offset < length) {
            const float tileLength = std::min(64.0f, length - offset);
            const Vector2 tileStart = Vector2Add(start, Vector2Scale(direction, offset));
            const Rectangle source = {0.0f, 0.0f, tileLength, 24.0f};
            const Rectangle destination = {tileStart.x, tileStart.y - 12.0f, tileLength, 24.0f};
            DrawTexturePro(fenceTexture_, source, destination, {0.0f, 12.0f}, rotation, WHITE);
            offset += tileLength;
        }
        return;
    }
    DrawLineEx(start, end, 12.0f, base);
    DrawLineEx(Vector2Add(start, Vector2Scale(normal, 7.0f)), Vector2Add(end, Vector2Scale(normal, 7.0f)), 7.0f, highlight);

    const float length = Vector2Distance(start, end);
    const int postCount = std::max(1, static_cast<int>(length / 66.0f));
    for (int i = 0; i <= postCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(postCount);
        const Vector2 center = LerpVector(start, end, t);
        DrawLineEx(
            Vector2Add(center, Vector2Scale(normal, -11.0f)),
            Vector2Add(center, Vector2Scale(normal, 11.0f)),
            6.0f,
            kFencePost);
    }
}

void SheepdogGame::LoadGameAssets() {
    const std::string appDirectory = GetApplicationDirectory();
    auto loadTexture = [](const std::array<std::string, 4>& paths) {
        for (const std::string& path : paths) {
            if (FileExists(path.c_str())) {
                return LoadTexture(path.c_str());
            }
        }
        return Texture2D{};
    };

    dogTexture_ = loadTexture({
        appDirectory + "../assets/dog.png",
        appDirectory + "assets/dog.png",
        "../assets/dog.png",
        "assets/dog.png",
    });
    sheepTexture_ = loadTexture({
        appDirectory + "../assets/sheep.png",
        appDirectory + "assets/sheep.png",
        "../assets/sheep.png",
        "assets/sheep.png",
    });
    fenceTexture_ = loadTexture({
        appDirectory + "../assets/fence.png",
        appDirectory + "assets/fence.png",
        "../assets/fence.png",
        "assets/fence.png",
    });
}

void SheepdogGame::UnloadGameAssets() {
    if (dogTexture_.id != 0) {
        UnloadTexture(dogTexture_);
    }
    if (sheepTexture_.id != 0) {
        UnloadTexture(sheepTexture_);
    }
    if (fenceTexture_.id != 0) {
        UnloadTexture(fenceTexture_);
    }
}

void SheepdogGame::PrepareAudio() {
    InitAudioDevice();
    audioReady_ = IsAudioDeviceReady();
    if (!audioReady_) {
        return;
    }

    Wave barkWave = MakeTone(145.0f, 0.16f, 0.28f);
    barkSound_ = LoadSoundFromWave(barkWave);
    UnloadWave(barkWave);
    SetSoundVolume(barkSound_, 0.55f);

    Wave penWave = MakeTone(640.0f, 0.18f, 0.22f);
    penSound_ = LoadSoundFromWave(penWave);
    UnloadWave(penWave);
    SetSoundVolume(penSound_, 0.45f);

    Wave winWave = MakeTone(760.0f, 0.42f, 0.25f);
    winSound_ = LoadSoundFromWave(winWave);
    UnloadWave(winWave);
    SetSoundVolume(winSound_, 0.55f);
}
