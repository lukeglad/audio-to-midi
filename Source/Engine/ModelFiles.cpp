#include "ModelFiles.h"

#include <juce_data_structures/juce_data_structures.h>

namespace atm
{

namespace
{
    // Pinned to one commit of the published repo (CC BY-NC 4.0). New weights
    // mean a new commit and new entries, changed together.
    constexpr const char* kModelRepo      = "DamRsn/muscriptor-gguf";
    constexpr const char* kModelRevision  = "d7045f94e8b19427f4ff9542975035e66596e51c";
    constexpr const char* kModelDirectory = "v1";   // the checkpoint format version

    constexpr std::array<ModelInfo, 3> kModels {{
        { ModelSize::small,  "small",  "muscriptor-small-f16.gguf",  209425152,
          "925f55af65a20ebc4f8b45ceaf095a12b72493d436cb112623cd0041a1af23d4", "210 MB" },
        { ModelSize::medium, "medium", "muscriptor-medium-f16.gguf", 618442496,
          "3850cc9e5b436b17a09bd25b8f2615cb3366ab96a71e7b50f73a793a917fdf03", "620 MB" },
        { ModelSize::large,  "large",  "muscriptor-large-f16.gguf",  2739142176,
          "35a750fb1ab1e77195cdc2c0b9b4aeea2f4d59f11f729f02af9920c4854ef72e", "2.7 GB" },
    }};

    juce::PropertiesFile& settings()
    {
        static juce::PropertiesFile file ([]
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = "audio-to-midi";   // fixed; the product name may change
            options.filenameSuffix      = "settings";
            options.folderName          = "AudioFlower";
            options.osxLibrarySubFolder = "Application Support";
            options.millisecondsBeforeSaving = 0;             // save synchronously, no timer needed
            return options;
        }());
        return file;
    }
}

const ModelInfo& modelInfo (ModelSize size)
{
    return kModels[(size_t) size];
}

std::optional<ModelSize> modelSizeFromName (const juce::String& name)
{
    for (const auto& model : kModels)
        if (name == model.name)
            return model.size;

    return std::nullopt;
}

namespace
{
    juce::File& directoryOverride()
    {
        static juce::File directory;
        return directory;
    }
}

juce::File modelsDirectory()
{
    if (directoryOverride() != juce::File())
        return directoryOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/AudioFlower/models");
}

void overrideModelsDirectory (const juce::File& directory)
{
    directoryOverride() = directory;
}

juce::File modelFile (ModelSize size)
{
    return modelsDirectory().getChildFile (modelInfo (size).fileName);
}

juce::File modelPartFile (ModelSize size)
{
    const auto& info = modelInfo (size);
    return modelsDirectory().getChildFile (juce::String (info.fileName) + "."
                                           + juce::String (info.sha256).substring (0, 8) + ".part");
}

juce::URL modelUrl (ModelSize size)
{
    return juce::URL ("https://huggingface.co/" + juce::String (kModelRepo) + "/resolve/" + kModelRevision
                      + "/" + kModelDirectory + "/" + modelInfo (size).fileName);
}

bool isModelInstalled (ModelSize size)
{
    // getSize() is 0 for a missing file, and no checkpoint is empty.
    return modelFile (size).getSize() == modelInfo (size).numBytes;
}

std::optional<ModelSize> bestInstalledModel (ModelSize preferred)
{
    if (isModelInstalled (preferred))
        return preferred;

    if (isModelInstalled (kDefaultModelSize))
        return kDefaultModelSize;

    for (const auto size : kAllModelSizes)
        if (isModelInstalled (size))
            return size;

    return std::nullopt;
}

std::filesystem::path toPath (const juce::File& file)
{
    return { file.getFullPathName().toStdString() };
}

const char* modelDisplayName (ModelSize size)
{
    switch (size)
    {
        case ModelSize::small:  return "fast";
        case ModelSize::medium: return "balanced";
        case ModelSize::large:  return "best";
    }
    return "balanced";
}

ModelSize preferredModel()
{
    return modelSizeFromName (settings().getValue ("model")).value_or (kDefaultModelSize);
}

void setPreferredModel (ModelSize size)
{
    settings().setValue ("model", modelInfo (size).name);
}

} // namespace atm
