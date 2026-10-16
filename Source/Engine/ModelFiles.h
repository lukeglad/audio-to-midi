#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <filesystem>
#include <optional>

//==============================================================================
/**
    The model checkpoints: which exist, where they come from, and where they
    live on disk. Nothing here touches the engine.

    Weights are not bundled with the plugin (they are large, and licensed
    separately: CC BY-NC 4.0); they are downloaded on demand into a folder shared by every AudioFlower plugin that might use
    them, so a renamed product never strands a 600 MB file.
*/
namespace atm
{
    enum class ModelSize { small, medium, large };

    inline constexpr std::array<ModelSize, 3> kAllModelSizes { ModelSize::small, ModelSize::medium, ModelSize::large };
    inline constexpr ModelSize kDefaultModelSize = ModelSize::medium;

    struct ModelInfo
    {
        ModelSize   size;
        const char* name;        // "small" / "medium" / "large": the settings value and CLI argument
        const char* fileName;    // as published
        juce::int64 numBytes;
        const char* sha256;      // compiled in, so a changed upstream file fails instead of being trusted
        const char* sizeLabel;   // "210 MB"
    };

    const ModelInfo& modelInfo (ModelSize);
    std::optional<ModelSize> modelSizeFromName (const juce::String&);

    /** ~/Library/Application Support/AudioFlower/models */
    juce::File modelsDirectory();

    /** atm-cli's download check only: points modelsDirectory() at a scratch
        folder, so the real models are never touched. Call before any download. */
    void overrideModelsDirectory (const juce::File&);
    juce::File modelFile (ModelSize);

    /** Where an unfinished download accumulates. Named for the digest, so a
        part started by a build pinned to other weights is never resumed. */
    juce::File modelPartFile (ModelSize);

    juce::URL modelUrl (ModelSize);

    /** True when the file is present with exactly the published size. The
        digest is only checked once, at download time. */
    bool isModelInstalled (ModelSize);

    std::filesystem::path toPath (const juce::File&);

    //==========================================================================
    /** What the UI calls a size: the trade it makes, not the file it is
        (fast / balanced / best). Files and settings keep `name`. */
    const char* modelDisplayName (ModelSize);

    // The chosen model is a per-user preference, not per-instance state: it
    // says which file is installed, and every instance should agree.
    ModelSize preferredModel();
    void setPreferredModel (ModelSize);
}
