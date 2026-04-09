#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace TrueFlightApp
{

    enum class RuntimeProfilerStage : std::size_t
    {
        EventPump = 0,
        Pressure = 1,
        Platform = 2,
        Input = 3,
        Terrain = 4,
        Simulation = 5,
        SceneAssembly = 6,
        Hud = 7,
        ImGui = 8,
        Render = 9,
        Save = 10,
        Count = 11
    };

    inline constexpr std::size_t kRuntimeProfilerStageCount = static_cast<std::size_t>(RuntimeProfilerStage::Count);

    inline constexpr std::array<std::string_view, kRuntimeProfilerStageCount> kRuntimeProfilerStageLabels{
        "Event",
        "Pressure",
        "Platform",
        "Input",
        "Terrain",
        "Sim",
        "Scene",
        "HUD",
        "ImGui",
        "Render",
        "Save"};

    inline constexpr std::size_t runtimeProfilerStageIndex(RuntimeProfilerStage stage)
    {
        return static_cast<std::size_t>(stage);
    }

    struct RuntimeProfilerFrameSample
    {
        bool inFlight = false;
        bool menuVisible = false;
        float fps = 0.0f;
        float profilerSelfMs = 0.0f;
        int opaqueObjectCount = 0;
        int translucentObjectCount = 0;
        int remotePeerCount = 0;
        int notificationCount = 0;
        int terrainQueuedCount = 0;
        int terrainInflightCount = 0;
        int terrainCompletedCount = 0;
        std::uint64_t terrainDroppedRequestCount = 0;
        std::uint64_t terrainDroppedResultCount = 0;
        std::uint64_t terrainStaleResultCount = 0;
        std::uint64_t terrainAdoptedResultCount = 0;
        std::uint64_t terrainWorkerBuildCount = 0;
        float terrainAdoptionMs = 0.0f;
        float terrainWorkerBuildMs = 0.0f;
        float terrainSyncBuildMs = 0.0f;
        std::size_t rendererResidentMeshBytes = 0;
        std::size_t rendererResidentMeshBudgetBytes = 0;
        std::size_t rendererSceneTextureBytes = 0;
        std::size_t rendererSceneTextureBudgetBytes = 0;
        std::size_t rendererFramebufferBytes = 0;
        std::size_t rendererTransientBufferBytes = 0;
        std::size_t rendererUploadBytes = 0;
        std::size_t rendererUploadBudgetBytes = 0;
        bool pressureValid = false;
        std::uint64_t ramFreeBytes = 0;
        std::uint64_t commitHeadroomBytes = 0;
        std::uint64_t processWorkingSetBytes = 0;
        std::uint64_t processPrivateBytes = 0;
        std::uint64_t gpuLocalUsageBytes = 0;
        std::uint64_t gpuLocalBudgetBytes = 0;
        std::uint64_t gpuSharedUsageBytes = 0;
        std::uint64_t gpuSharedBudgetBytes = 0;
    };

    class RuntimeProfiler
    {
    public:
        static constexpr std::size_t kMaxHistoryFrames = 600u;
        static constexpr std::size_t kSummaryWindowFrames = 240u;
        static constexpr std::size_t kGraphSamples = 48u;

        struct StageSummary
        {
            RuntimeProfilerStage stage = RuntimeProfilerStage::EventPump;
            float lastMs = 0.0f;
            float avgMs = 0.0f;
            float p95Ms = 0.0f;
            float maxMs = 0.0f;
        };

        class ScopedStage
        {
        public:
            ScopedStage() = default;

            ScopedStage(RuntimeProfiler *profiler, RuntimeProfilerStage stage)
                : profiler_(profiler),
                  stage_(stage),
                  startCounter_(SDL_GetPerformanceCounter())
            {
            }

            ScopedStage(const ScopedStage &) = delete;
            ScopedStage &operator=(const ScopedStage &) = delete;

            ScopedStage(ScopedStage &&other) noexcept
                : profiler_(other.profiler_),
                  stage_(other.stage_),
                  startCounter_(other.startCounter_),
                  active_(other.active_)
            {
                other.active_ = false;
                other.profiler_ = nullptr;
            }

            ScopedStage &operator=(ScopedStage &&other) noexcept
            {
                if (this != &other)
                {
                    finish();
                    profiler_ = other.profiler_;
                    stage_ = other.stage_;
                    startCounter_ = other.startCounter_;
                    active_ = other.active_;
                    other.active_ = false;
                    other.profiler_ = nullptr;
                }
                return *this;
            }

            ~ScopedStage()
            {
                finish();
            }

            void finish()
            {
                if (!active_ || profiler_ == nullptr)
                {
                    return;
                }
                profiler_->addStageDuration(stage_, profiler_->counterDeltaMs(startCounter_, SDL_GetPerformanceCounter()));
                active_ = false;
            }

        private:
            RuntimeProfiler *profiler_ = nullptr;
            RuntimeProfilerStage stage_ = RuntimeProfilerStage::EventPump;
            std::uint64_t startCounter_ = 0u;
            bool active_ = true;
        };

        class ScopedProfilerSelfCost
        {
        public:
            ScopedProfilerSelfCost() = default;

            explicit ScopedProfilerSelfCost(const RuntimeProfiler *profiler)
                : profiler_(profiler),
                  startCounter_(SDL_GetPerformanceCounter())
            {
            }

            ScopedProfilerSelfCost(const ScopedProfilerSelfCost &) = delete;
            ScopedProfilerSelfCost &operator=(const ScopedProfilerSelfCost &) = delete;

            ScopedProfilerSelfCost(ScopedProfilerSelfCost &&other) noexcept
                : profiler_(other.profiler_),
                  startCounter_(other.startCounter_),
                  active_(other.active_)
            {
                other.active_ = false;
                other.profiler_ = nullptr;
            }

            ScopedProfilerSelfCost &operator=(ScopedProfilerSelfCost &&other) noexcept
            {
                if (this != &other)
                {
                    finish();
                    profiler_ = other.profiler_;
                    startCounter_ = other.startCounter_;
                    active_ = other.active_;
                    other.active_ = false;
                    other.profiler_ = nullptr;
                }
                return *this;
            }

            ~ScopedProfilerSelfCost()
            {
                finish();
            }

            void finish()
            {
                if (!active_ || profiler_ == nullptr)
                {
                    return;
                }
                profiler_->addProfilerSelfDuration(profiler_->counterDeltaMs(startCounter_, SDL_GetPerformanceCounter()));
                active_ = false;
            }

        private:
            const RuntimeProfiler *profiler_ = nullptr;
            std::uint64_t startCounter_ = 0u;
            bool active_ = true;
        };

        RuntimeProfiler()
            : counterFrequency_(static_cast<double>(SDL_GetPerformanceFrequency()))
        {
        }

        void beginFrame(double appTimeSeconds, bool inFlight, bool menuVisible)
        {
            frameActive_ = true;
            frameStartCounter_ = SDL_GetPerformanceCounter();
            currentAppTimeSeconds_ = appTimeSeconds;
            currentStageMs_.fill(0.0f);
            currentProfilerSelfMs_ = 0.0f;
            currentSample_ = {};
            currentSample_.inFlight = inFlight;
            currentSample_.menuVisible = menuVisible;
        }

        [[nodiscard]] ScopedStage scope(RuntimeProfilerStage stage)
        {
            return ScopedStage(this, stage);
        }

        [[nodiscard]] ScopedProfilerSelfCost scopeProfilerSelfCost() const
        {
            return ScopedProfilerSelfCost(this);
        }

        void addStageDuration(RuntimeProfilerStage stage, float durationMs)
        {
            if (!frameActive_)
            {
                return;
            }
            currentStageMs_[runtimeProfilerStageIndex(stage)] += std::max(0.0f, durationMs);
        }

        void addProfilerSelfDuration(float durationMs) const
        {
            if (!frameActive_)
            {
                return;
            }
            currentProfilerSelfMs_ += std::max(0.0f, durationMs);
        }

        void endFrame(const RuntimeProfilerFrameSample &sample)
        {
            if (!frameActive_)
            {
                return;
            }

            const std::uint64_t frameCompletionStartCounter = SDL_GetPerformanceCounter();
            currentSample_ = sample;
            currentSample_.inFlight = sample.inFlight;
            currentSample_.menuVisible = sample.menuVisible;
            currentSample_.fps = sample.fps;

            FrameRecord record{};
            record.frameIndex = ++frameCounter_;
            record.appTimeSeconds = currentAppTimeSeconds_;
            record.wallClockUnixSeconds = currentUnixSeconds();
            record.stageMs = currentStageMs_;

            history_.push_back(record);
            while (history_.size() > kMaxHistoryFrames)
            {
                history_.pop_front();
            }

            const std::uint64_t frameCompletionEndCounter = SDL_GetPerformanceCounter();
            currentProfilerSelfMs_ += counterDeltaMs(frameCompletionStartCounter, frameCompletionEndCounter);
            currentSample_.profilerSelfMs = std::max(0.0f, sample.profilerSelfMs) + currentProfilerSelfMs_;
            record.frameMs = counterDeltaMs(frameStartCounter_, frameCompletionEndCounter);
            record.sample = currentSample_;
            if (!history_.empty())
            {
                history_.back() = record;
            }

            if (captureFile_.is_open())
            {
                writeCsvRow(captureFile_, record);
                ++captureFrameCount_;
                if ((captureFrameCount_ % 60u) == 0u)
                {
                    captureFile_.flush();
                }
            }

            frameActive_ = false;
            currentProfilerSelfMs_ = 0.0f;
        }

        [[nodiscard]] bool isCapturing() const
        {
            return captureFile_.is_open();
        }

        [[nodiscard]] std::size_t historyFrameCount() const
        {
            return history_.size();
        }

        [[nodiscard]] const std::filesystem::path &capturePath() const
        {
            return capturePath_;
        }

        void clearHistory()
        {
            history_.clear();
            currentProfilerSelfMs_ = 0.0f;
        }

        bool startCsvCapture(
            const std::filesystem::path &directory,
            std::string_view stem,
            std::filesystem::path *capturePathOut,
            std::string *errorText)
        {
            if (captureFile_.is_open())
            {
                if (capturePathOut != nullptr)
                {
                    *capturePathOut = capturePath_;
                }
                return true;
            }

            if (directory.empty())
            {
                if (errorText != nullptr)
                {
                    *errorText = "Profiler capture directory is unavailable.";
                }
                return false;
            }

            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            if (ec)
            {
                if (errorText != nullptr)
                {
                    *errorText = "Could not create profiler capture directory: " + directory.string();
                }
                return false;
            }

            const std::filesystem::path outputPath = makeUniqueCapturePath(directory, stem);
            std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                if (errorText != nullptr)
                {
                    *errorText = "Could not open " + outputPath.string() + " for writing.";
                }
                return false;
            }

            writeCsvHeader(file);
            if (!file.good())
            {
                if (errorText != nullptr)
                {
                    *errorText = "Failed while writing profiler CSV header to " + outputPath.string() + ".";
                }
                return false;
            }

            captureFile_ = std::move(file);
            capturePath_ = outputPath;
            captureFrameCount_ = 0u;
            if (capturePathOut != nullptr)
            {
                *capturePathOut = capturePath_;
            }
            return true;
        }

        bool stopCsvCapture(std::filesystem::path *capturePathOut, std::string *errorText)
        {
            if (!captureFile_.is_open())
            {
                if (capturePathOut != nullptr)
                {
                    capturePathOut->clear();
                }
                return true;
            }

            captureFile_.flush();
            const bool success = captureFile_.good();
            captureFile_.close();
            if (capturePathOut != nullptr)
            {
                *capturePathOut = capturePath_;
            }

            if (!success && errorText != nullptr)
            {
                *errorText = "Failed while finalizing profiler CSV " + capturePath_.string() + ".";
            }

            return success;
        }

        bool exportHistoryCsv(
            const std::filesystem::path &directory,
            std::string_view stem,
            std::filesystem::path *capturePathOut,
            std::string *errorText) const
        {
            if (directory.empty())
            {
                if (errorText != nullptr)
                {
                    *errorText = "Profiler capture directory is unavailable.";
                }
                return false;
            }

            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            if (ec)
            {
                if (errorText != nullptr)
                {
                    *errorText = "Could not create profiler capture directory: " + directory.string();
                }
                return false;
            }

            const std::filesystem::path outputPath = makeUniqueCapturePath(directory, stem);
            std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                if (errorText != nullptr)
                {
                    *errorText = "Could not open " + outputPath.string() + " for writing.";
                }
                return false;
            }

            writeCsvHeader(file);
            for (const FrameRecord &record : history_)
            {
                writeCsvRow(file, record);
            }

            file.flush();
            if (!file.good())
            {
                if (errorText != nullptr)
                {
                    *errorText = "Failed while writing profiler history to " + outputPath.string() + ".";
                }
                return false;
            }

            if (capturePathOut != nullptr)
            {
                *capturePathOut = outputPath;
            }
            return true;
        }

        [[nodiscard]] std::vector<std::string> buildOverlayLines(std::size_t maxStageCount = 3u) const
        {
            std::vector<std::string> lines;
            buildOverlayLines(maxStageCount, lines);
            return lines;
        }

        void buildOverlayLines(std::size_t maxStageCount, std::vector<std::string> &lines) const
        {
            ScopedProfilerSelfCost selfCost(this);
            buildOverlayLinesImpl(maxStageCount, lines);
        }

        [[nodiscard]] std::vector<StageSummary> stageSummaries(std::size_t maxStageCount) const
        {
            std::vector<StageSummary> summaries;
            stageSummaries(maxStageCount, summaries);
            return summaries;
        }

        void stageSummaries(std::size_t maxStageCount, std::vector<StageSummary> &summaries) const
        {
            ScopedProfilerSelfCost selfCost(this);
            stageSummariesImpl(maxStageCount, summaries);
        }

    private:
        void buildOverlayLinesImpl(std::size_t maxStageCount, std::vector<std::string> &lines) const
        {
            lines.clear();
            lines.reserve(5u + maxStageCount);
            if (history_.empty())
            {
                lines.push_back("Profiler: warming up");
                lines.push_back(isCapturing()
                                    ? ("CSV recording: " + capturePath_.filename().string())
                                    : "CSV idle | F9 record | F10 export history | F11 reset");
                return;
            }

            const std::size_t beginIndex = history_.size() > kSummaryWindowFrames ? (history_.size() - kSummaryWindowFrames) : 0u;
            frameSampleScratch_.clear();
            frameSampleScratch_.reserve(history_.size() - beginIndex);
            for (std::size_t i = beginIndex; i < history_.size(); ++i)
            {
                frameSampleScratch_.push_back(history_[i].frameMs);
            }

            const FrameRecord &last = history_.back();
            const float avgFrameMs = average(frameSampleScratch_);
            percentileScratch_ = frameSampleScratch_;
            const float p95FrameMs = percentileInPlace(percentileScratch_, 0.95f);
            const float maxFrameMs = maximum(frameSampleScratch_);

            std::string statusLine = "Profiler " + std::to_string(history_.size()) + "f";
            statusLine += isCapturing()
                              ? (" | CSV REC " + capturePath_.filename().string())
                              : " | CSV idle";
            lines.push_back(std::move(statusLine));

            lines.push_back(
                "CPU avg " + formatFixed(avgFrameMs, 2) + " ms" +
                " | p95 " + formatFixed(p95FrameMs, 2) + " ms" +
                " | max " + formatFixed(maxFrameMs, 2) + " ms" +
                " | last " + formatFixed(last.frameMs, 2) + " ms" +
                " | self " + formatFixed(last.sample.profilerSelfMs, 2) + " ms" +
                " | fps " + formatFixed(last.sample.fps, 1));

            lines.push_back("Trend " + buildSparkline(frameSampleScratch_, stageSampleScratch_, percentileScratch_));

            stageSummaryScratch_.clear();
            stageSummariesImpl(maxStageCount, stageSummaryScratch_);
            if (!stageSummaryScratch_.empty())
            {
                std::string hotLine = "Hot avg ";
                for (std::size_t i = 0; i < stageSummaryScratch_.size(); ++i)
                {
                    if (i > 0u)
                    {
                        hotLine += " | ";
                    }
                    const StageSummary &stage = stageSummaryScratch_[i];
                    hotLine += std::string(kRuntimeProfilerStageLabels[runtimeProfilerStageIndex(stage.stage)]);
                    hotLine += " ";
                    hotLine += formatFixed(stage.avgMs, 2);
                    hotLine += " ms";
                }
                lines.push_back(std::move(hotLine));
            }

            std::string lastLine = "Last";
            for (std::size_t stageIndex = 0; stageIndex < kRuntimeProfilerStageCount; ++stageIndex)
            {
                const float stageMs = last.stageMs[stageIndex];
                if (stageMs <= 0.02f)
                {
                    continue;
                }
                lastLine += " | ";
                lastLine += std::string(kRuntimeProfilerStageLabels[stageIndex]);
                lastLine += " ";
                lastLine += formatFixed(stageMs, 2);
            }
            lastLine += " | profiler self ";
            lastLine += formatFixed(last.sample.profilerSelfMs, 2);
            lines.push_back(std::move(lastLine));
        }

        void stageSummariesImpl(std::size_t maxStageCount, std::vector<StageSummary> &summaries) const
        {
            summaries.clear();
            if (history_.empty())
            {
                return;
            }

            const std::size_t beginIndex = history_.size() > kSummaryWindowFrames ? (history_.size() - kSummaryWindowFrames) : 0u;
            const FrameRecord &last = history_.back();
            summaries.reserve(kRuntimeProfilerStageCount);

            for (std::size_t stageIndex = 0; stageIndex < kRuntimeProfilerStageCount; ++stageIndex)
            {
                stageSampleScratch_.clear();
                stageSampleScratch_.reserve(history_.size() - beginIndex);
                for (std::size_t i = beginIndex; i < history_.size(); ++i)
                {
                    stageSampleScratch_.push_back(history_[i].stageMs[stageIndex]);
                }

                const float avgMs = average(stageSampleScratch_);
                if (avgMs <= 0.02f)
                {
                    continue;
                }

                StageSummary summary{};
                summary.stage = static_cast<RuntimeProfilerStage>(stageIndex);
                summary.lastMs = last.stageMs[stageIndex];
                summary.avgMs = avgMs;
                summary.p95Ms = percentileInPlace(stageSampleScratch_, 0.95f);
                summary.maxMs = maximum(stageSampleScratch_);
                summaries.push_back(summary);
            }

            std::sort(
                summaries.begin(),
                summaries.end(),
                [](const StageSummary &lhs, const StageSummary &rhs)
                {
                    return lhs.avgMs > rhs.avgMs;
                });

            if (summaries.size() > maxStageCount)
            {
                summaries.resize(maxStageCount);
            }
        }

    private:
        struct FrameRecord
        {
            std::uint64_t frameIndex = 0u;
            double appTimeSeconds = 0.0;
            double wallClockUnixSeconds = 0.0;
            float frameMs = 0.0f;
            std::array<float, kRuntimeProfilerStageCount> stageMs{};
            RuntimeProfilerFrameSample sample{};
        };

        static double currentUnixSeconds()
        {
            using clock = std::chrono::system_clock;
            return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
        }

        [[nodiscard]] float counterDeltaMs(std::uint64_t startCounter, std::uint64_t endCounter) const
        {
            if (endCounter <= startCounter || counterFrequency_ <= 0.0)
            {
                return 0.0f;
            }
            const double seconds = static_cast<double>(endCounter - startCounter) / counterFrequency_;
            return static_cast<float>(seconds * 1000.0);
        }

        static std::string formatFixed(double value, int precision)
        {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(precision) << value;
            return stream.str();
        }

        static float average(const std::vector<float> &values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            const float sum = std::accumulate(values.begin(), values.end(), 0.0f);
            return sum / static_cast<float>(values.size());
        }

        static float maximum(const std::vector<float> &values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            return *std::max_element(values.begin(), values.end());
        }

        static float percentile(std::vector<float> values, float p)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            std::sort(values.begin(), values.end());
            const float clampedP = std::clamp(p, 0.0f, 1.0f);
            const float scaledIndex = clampedP * static_cast<float>(values.size() - 1u);
            const std::size_t lowerIndex = static_cast<std::size_t>(std::floor(scaledIndex));
            const std::size_t upperIndex = static_cast<std::size_t>(std::ceil(scaledIndex));
            const float blend = scaledIndex - static_cast<float>(lowerIndex);
            return (values[lowerIndex] * (1.0f - blend)) + (values[upperIndex] * blend);
        }

        static float percentileInPlace(std::vector<float> &values, float p)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            std::sort(values.begin(), values.end());
            const float clampedP = std::clamp(p, 0.0f, 1.0f);
            const float scaledIndex = clampedP * static_cast<float>(values.size() - 1u);
            const std::size_t lowerIndex = static_cast<std::size_t>(std::floor(scaledIndex));
            const std::size_t upperIndex = static_cast<std::size_t>(std::ceil(scaledIndex));
            const float blend = scaledIndex - static_cast<float>(lowerIndex);
            return (values[lowerIndex] * (1.0f - blend)) + (values[upperIndex] * blend);
        }

        static std::string buildSparkline(
            const std::vector<float> &samples,
            std::vector<float> &recentScratch,
            std::vector<float> &percentileScratch)
        {
            if (samples.empty())
            {
                return {};
            }

            static constexpr std::string_view glyphs = " .:-=+*#%@";
            const std::size_t count = std::min<std::size_t>(kGraphSamples, samples.size());
            const std::size_t firstIndex = samples.size() - count;
            recentScratch.assign(samples.begin() + static_cast<std::ptrdiff_t>(firstIndex), samples.end());
            percentileScratch = recentScratch;
            const float scale = std::max(16.0f, percentileInPlace(percentileScratch, 0.95f) * 1.25f);

            std::string sparkline;
            sparkline.reserve(count);
            for (float value : recentScratch)
            {
                const float normalized = std::clamp(value / scale, 0.0f, 1.0f);
                const std::size_t glyphIndex = static_cast<std::size_t>(std::round(normalized * static_cast<float>(glyphs.size() - 1u)));
                sparkline.push_back(glyphs[glyphIndex]);
            }

            return sparkline + " (" + formatFixed(scale, 1) + " ms scale)";
        }

        static std::string captureTimestampToken()
        {
            const double unixSeconds = currentUnixSeconds();
            const std::time_t timeValue = static_cast<std::time_t>(unixSeconds);
            std::tm utcTime{};
#ifdef _WIN32
            gmtime_s(&utcTime, &timeValue);
#else
            gmtime_r(&timeValue, &utcTime);
#endif
            char buffer[32]{};
            std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &utcTime);
            return buffer;
        }

        static std::string formatUtcTimestamp(double unixSeconds)
        {
            const double integralSeconds = std::floor(unixSeconds);
            const int millis = static_cast<int>(std::round((unixSeconds - integralSeconds) * 1000.0));
            const std::time_t timeValue = static_cast<std::time_t>(integralSeconds);
            std::tm utcTime{};
#ifdef _WIN32
            gmtime_s(&utcTime, &timeValue);
#else
            gmtime_r(&timeValue, &utcTime);
#endif
            char dateBuffer[32]{};
            std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%dT%H:%M:%S", &utcTime);
            char finalBuffer[40]{};
            std::snprintf(finalBuffer, sizeof(finalBuffer), "%s.%03dZ", dateBuffer, std::clamp(millis, 0, 999));
            return finalBuffer;
        }

        static std::filesystem::path makeUniqueCapturePath(const std::filesystem::path &directory, std::string_view stem)
        {
            const std::string timestamp = captureTimestampToken();
            for (int suffix = 0; suffix < 1000; ++suffix)
            {
                std::string fileName(stem);
                fileName += "_";
                fileName += timestamp;
                if (suffix > 0)
                {
                    fileName += "_";
                    fileName += std::to_string(suffix);
                }
                fileName += ".csv";

                const std::filesystem::path candidate = directory / fileName;
                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec))
                {
                    return candidate;
                }
            }

            return directory / (std::string(stem) + "_" + timestamp + "_overflow.csv");
        }

        static void writeCsvHeader(std::ostream &stream)
        {
            stream
                << "timestamp_utc,frame_index,app_time_seconds,frame_ms,profiler_self_ms,fps,in_flight,menu_visible,"
                << "event_ms,pressure_ms,platform_ms,input_ms,terrain_ms,simulation_ms,scene_ms,hud_ms,imgui_ms,render_ms,save_ms,"
                << "opaque_objects,translucent_objects,remote_peers,notifications,"
                << "terrain_queued,terrain_inflight,terrain_completed,terrain_drop_requests,terrain_drop_results,terrain_stale_results,terrain_adopted,terrain_worker_builds,terrain_adopt_ms,terrain_worker_build_ms,terrain_sync_build_ms,"
                << "renderer_mesh_bytes,renderer_mesh_budget_bytes,renderer_texture_bytes,renderer_texture_budget_bytes,renderer_framebuffer_bytes,renderer_transient_bytes,renderer_upload_bytes,renderer_upload_budget_bytes,"
                << "pressure_valid,ram_free_bytes,commit_headroom_bytes,process_working_set_bytes,process_private_bytes,gpu_local_usage_bytes,gpu_local_budget_bytes,gpu_shared_usage_bytes,gpu_shared_budget_bytes\n";
        }

        static void writeCsvRow(std::ostream &stream, const FrameRecord &record)
        {
            stream
                << formatUtcTimestamp(record.wallClockUnixSeconds) << ","
                << record.frameIndex << ","
                << formatFixed(record.appTimeSeconds, 6) << ","
                << formatFixed(record.frameMs, 4) << ","
                << formatFixed(record.sample.profilerSelfMs, 4) << ","
                << formatFixed(record.sample.fps, 3) << ","
                << (record.sample.inFlight ? 1 : 0) << ","
                << (record.sample.menuVisible ? 1 : 0) << ",";

            for (std::size_t stageIndex = 0; stageIndex < kRuntimeProfilerStageCount; ++stageIndex)
            {
                stream << formatFixed(record.stageMs[stageIndex], 4) << ",";
            }

            stream
                << record.sample.opaqueObjectCount << ","
                << record.sample.translucentObjectCount << ","
                << record.sample.remotePeerCount << ","
                << record.sample.notificationCount << ","
                << record.sample.terrainQueuedCount << ","
                << record.sample.terrainInflightCount << ","
                << record.sample.terrainCompletedCount << ","
                << record.sample.terrainDroppedRequestCount << ","
                << record.sample.terrainDroppedResultCount << ","
                << record.sample.terrainStaleResultCount << ","
                << record.sample.terrainAdoptedResultCount << ","
                << record.sample.terrainWorkerBuildCount << ","
                << formatFixed(record.sample.terrainAdoptionMs, 4) << ","
                << formatFixed(record.sample.terrainWorkerBuildMs, 4) << ","
                << formatFixed(record.sample.terrainSyncBuildMs, 4) << ","
                << record.sample.rendererResidentMeshBytes << ","
                << record.sample.rendererResidentMeshBudgetBytes << ","
                << record.sample.rendererSceneTextureBytes << ","
                << record.sample.rendererSceneTextureBudgetBytes << ","
                << record.sample.rendererFramebufferBytes << ","
                << record.sample.rendererTransientBufferBytes << ","
                << record.sample.rendererUploadBytes << ","
                << record.sample.rendererUploadBudgetBytes << ","
                << (record.sample.pressureValid ? 1 : 0) << ","
                << record.sample.ramFreeBytes << ","
                << record.sample.commitHeadroomBytes << ","
                << record.sample.processWorkingSetBytes << ","
                << record.sample.processPrivateBytes << ","
                << record.sample.gpuLocalUsageBytes << ","
                << record.sample.gpuLocalBudgetBytes << ","
                << record.sample.gpuSharedUsageBytes << ","
                << record.sample.gpuSharedBudgetBytes << "\n";
        }

        double counterFrequency_ = 0.0;
        bool frameActive_ = false;
        std::uint64_t frameStartCounter_ = 0u;
        std::uint64_t frameCounter_ = 0u;
        std::size_t captureFrameCount_ = 0u;
        double currentAppTimeSeconds_ = 0.0;
        mutable float currentProfilerSelfMs_ = 0.0f;
        std::array<float, kRuntimeProfilerStageCount> currentStageMs_{};
        RuntimeProfilerFrameSample currentSample_{};
        std::deque<FrameRecord> history_{};
        std::ofstream captureFile_{};
        std::filesystem::path capturePath_{};
        mutable std::vector<float> frameSampleScratch_{};
        mutable std::vector<float> percentileScratch_{};
        mutable std::vector<float> stageSampleScratch_{};
        mutable std::vector<StageSummary> stageSummaryScratch_{};
    };

} // namespace TrueFlightApp
