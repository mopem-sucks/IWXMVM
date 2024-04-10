#include "StdInclude.hpp"
#include "Playback.hpp"

#include "Mod.hpp"
#include "Rewinding.hpp"

namespace IWXMVM::Components::Playback
{
    bool isPlaybackPaused = false;

    void TogglePaused()
    {
        isPlaybackPaused = !isPlaybackPaused;
    }

    bool IsPaused()
    {
        return isPlaybackPaused;
    }
    
    void SkipForward(std::int32_t ticks, bool ignoreFrozenTick)
    {
        if (!ignoreFrozenTick && Mod::GetGameInterface()->IsTickFrozen().has_value())
        {
            Mod::GetGameInterface()->UpdateFrozenTick(false, ticks);
            return;
        }

        auto addresses = Mod::GetGameInterface()->GetPlaybackDataAddresses();
        auto realtime = reinterpret_cast<int32_t*>(addresses.cls.realtime);
        *realtime = *realtime + ticks;
        LOG_DEBUG("Skipping forward {} ticks, realtime: {}", ticks, *realtime);
    }

    void SetTickDelta(int32_t value, bool ignoreDeadzone)
    {
        if (value > 0)
            SkipForward(value);
        else if ((value < -REWIND_DEADZONE) || (value < 0 && ignoreDeadzone))
            Rewinding::RewindBy(value);
    }

    void HandleImportedFrozenTickLogic(std::optional<std::uint32_t> frozenTick)
    {
        const auto demoInfo = Mod::GetGameInterface()->GetDemoInfo();

        if (!frozenTick.has_value())
        {
            if (demoInfo.frozenTick.has_value())
            {
                // toggle frozen tick off because it isn't set for the imported file
                Mod::GetGameInterface()->ToggleFrozenTick();
            }
            return;
        }

        if (demoInfo.frozenTick.has_value())
        {
            // toggle frozen tick off so we can rewind the actual state of the game
            Mod::GetGameInterface()->ToggleFrozenTick();
        }

        // set actual tick
        Playback::SetTickDelta(frozenTick.value() - demoInfo.currentTick, true);

        // (re)enable frozen tick with specified value
        Mod::GetGameInterface()->ToggleFrozenTick(frozenTick);

        // update current frozen tick
        Mod::GetGameInterface()->UpdateFrozenTick(false, frozenTick.value() - demoInfo.currentTick);

        if (!Playback::IsPaused())
        {
            Playback::TogglePaused();
        }
    }

    void GeneratePattern(auto& pattern, float fps, float timescale)
    {
        // SV_Frame is called once per frame, so we calculate the number of calls it takes to advance 1000 ms
        // by using the following formula: fps / timescale

        // example: 500 fps, 0.01 timescale
        // 500 / 0.01 = 50'000
        // we need to generate a pattern where 1000 * 1ms and 49000 * 0ms are interleaved, and fit this into an array of
        // size 1000

        const std::size_t callCountPerSecond =
            static_cast<std::size_t>(std::clamp(fps / timescale, 1000.0f, 1'000'000.0f));

        const std::size_t onesCount = static_cast<size_t>(pattern.size() / (callCountPerSecond / static_cast<float>(pattern.size())));
        const std::size_t zerosCount = pattern.size() - onesCount;

        for (std::size_t i = 0, zeros = 1, ones = 1; i < pattern.size(); ++i)
        {
            if (ones * zerosCount < zeros * onesCount)
            {
                ++ones;
                pattern[i] = 1;
            }
            else
            {
                ++zeros;
                pattern[i] = 0;
            }
        }

        assert(std::accumulate(pattern.begin(), pattern.end(), 0) > 0);
    }

    std::int32_t CalculatePlaybackDelta(std::int32_t gameMsec)
    {
        const bool useFrozenTick = Mod::GetGameInterface()->IsTickFrozen().has_value();

        // check if we need to skip forward for exact rewinding
        if (!useFrozenTick && Components::Rewinding::CheckSkipForward())
        {
            return 0;
        }

        auto& captureManager = Components::CaptureManager::Get();
        if (captureManager.IsCapturing())
        {
            if (useFrozenTick)
            {
                Mod::GetGameInterface()->UpdateFrozenTick(false, captureManager.OnGameFrame());
                return 0;
            }
            return captureManager.OnGameFrame();
        }

        // workaround for low timescales that would take a couple of seconds to trigger the rewind process to begin
        if (!useFrozenTick && Components::Rewinding::IsRewinding())
        {
            return 50;
        }

        // always return 0 msec when pausing demo playback
        if (!useFrozenTick && IsPaused())
        {
            return 0;
        }

        const std::optional<Types::Dvar> timescale = Mod::GetGameInterface()->GetDvar("timescale");

        // we can use the original msec value when its value is greater than 1, and/or when timescale is equal or
        // greater than 1.0
        if (gameMsec > 1 || !timescale.has_value() || timescale.value().value->floating_point >= 1.0f)
        {
            if (useFrozenTick)
            {
                Mod::GetGameInterface()->UpdateFrozenTick(IsPaused(), gameMsec);
                return 0;
            }
            return gameMsec;
        }

        const std::optional<Types::Dvar> com_maxfps = Mod::GetGameInterface()->GetDvar("com_maxfps");
        if (!com_maxfps.has_value())
        {
            if (useFrozenTick)
            {
                Mod::GetGameInterface()->UpdateFrozenTick(IsPaused(), gameMsec);
                return 0;
            }
            return gameMsec;
        }

        static float lastTimeScale = timescale.value().value->floating_point;
        static std::int32_t lastMaxFps = com_maxfps.value().value->int32;
        static std::array<std::uint8_t, 1000> pattern{};
        static std::size_t patternIndex = 0;

        // below we're going to generate a pattern of interleaved 0s and 1s based on (imgui) frame times
        // we generate a new pattern each second, or whenever timescale or com_maxfps values changes

        if (lastTimeScale != timescale.value().value->floating_point || lastMaxFps != com_maxfps.value().value->int32)
        {
            // this branch ensures that any change to the timescale or max fps immediately changes the pattern
            float frameRate;

            if (lastMaxFps >
                com_maxfps.value().value->int32)  // max fps was decreased, imgui fps is potentially too high
                frameRate = std::min(ImGui::GetIO().Framerate, static_cast<float>(com_maxfps.value().value->int32));
            else if (lastMaxFps <
                     com_maxfps.value().value->int32)  // max fps was increased, imgui fps is potentially too low
                frameRate = std::max(ImGui::GetIO().Framerate, static_cast<float>(com_maxfps.value().value->int32));
            else
            {
                assert(lastTimeScale != timescale.value().value->floating_point);
                frameRate = ImGui::GetIO().Framerate;
            }

            lastTimeScale = timescale.value().value->floating_point;
            lastMaxFps = com_maxfps.value().value->int32;
            patternIndex = 0;

            GeneratePattern(pattern, frameRate, timescale.value().value->floating_point);
        }
        else if (patternIndex % 1000 == 0)
            GeneratePattern(pattern, ImGui::GetIO().Framerate, timescale.value().value->floating_point);

        if (useFrozenTick)
        {
            Mod::GetGameInterface()->UpdateFrozenTick(IsPaused(), pattern[patternIndex++ % 1000]);
            return 0;
        }

        // advance (1ms) or pause(0ms) based on the pattern
        return pattern[patternIndex++ % 1000];
    }
}  // namespace IWXMVM::Components::Playback
