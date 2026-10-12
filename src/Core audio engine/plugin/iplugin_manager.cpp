#include "iplugin_manager.h"
#include "gm_synth_plugin.h"
#include "../../Core infrastructure/plugins/iplugin_scanner.h"
#include "../../Hardware/OS abstraction/filesystem/ifile_system.h"
#include "vst3_plugin_impl.h"
#include "clap_plugin_impl.h"
#include "au_plugin_impl.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>

namespace Layer3 {

namespace {

// DJB2 of the path, like scanned plugins, so built-in ids are stable across launches.
uint32_t stablePluginId(const char* path) {
    uint32_t hash = 5381;
    for (const char* c = path; *c != '\0'; ++c) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(*c);
    }
    return hash;
}

PluginDescriptor builtinDescriptor(const char* name, const char* path) {
    PluginDescriptor descriptor{};
    descriptor.pluginId = stablePluginId(path);
    std::strncpy(descriptor.name, name, sizeof(descriptor.name) - 1);
    std::strncpy(descriptor.manufacturer, "Stratawright", sizeof(descriptor.manufacturer) - 1);
    std::strncpy(descriptor.filePath, path, sizeof(descriptor.filePath) - 1);
    descriptor.version = 1u << 16;
    descriptor.formatFlags = PluginFormatFlags::BUILTIN;
    descriptor.capabilities = 0x01;  // instrument (the CLI checks this...)
    descriptor.category = PluginCategory::INSTRUMENT;  // (...the GUI checks this)
    return descriptor;
}

} // namespace

class PluginManagerImpl : public IPluginManager {
public:
    PluginManagerImpl() {
        // Phase 1: Initialize Dependencies
        Layer2::IPluginScanner::ScanConfig config;
        config.setDefaults();
        m_scanner = Layer2::IPluginScanner::create(config);
        
        // Load existing cache
        if (m_scanner->loadCache()) {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            m_registry = m_scanner->getCachedDescriptors();
        }

        // Built-in instruments are always available, whatever is installed. Scans only add or
        // replace entries by id, so these survive rescans.
        std::lock_guard<std::mutex> lock(m_registryMutex);
        m_registry.push_back(builtinDescriptor("GM Synth", GMSynthPlugin::kSynthPath));
        m_registry.push_back(builtinDescriptor("GM Drums", GMSynthPlugin::kDrumsPath));
    }

    ~PluginManagerImpl() override {
        if (m_scanner) {
            m_scanner->cancelScan();
        }
    }

    void scanForPlugins(const std::vector<std::string>& paths) override {
        // Phase 2: Discovery & Scanning Logic
        std::vector<std::string> finalPaths = paths;
        if (finalPaths.empty()) {
            finalPaths = getDefaultPaths();
        }

        for (const auto& path : finalPaths) {
            m_scanner->scanDirectory(path.c_str(), [](void* context, const Layer2::IPluginScanner::ScanResult& result) {
                auto* self = static_cast<PluginManagerImpl*>(context);
                if (result.success) {
                    std::lock_guard<std::mutex> lock(self->m_registryMutex);
                    // Update or add to registry
                    bool found = false;
                    for (auto& existing : self->m_registry) {
                        if (existing.pluginId == result.descriptor.pluginId) {
                            existing = result.descriptor;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        self->m_registry.push_back(result.descriptor);
                    }
                }

                // Check if all plugins in the current scan have been processed
                auto progress = self->m_scanner->getProgress();
                if (!progress.isScanning || progress.currentPlugin >= progress.totalPlugins) {
                    self->m_scanner->saveCache();
                }
            }, this);
        }
    }

    std::vector<PluginDescriptor> getAvailablePlugins() const override {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        return m_registry;
    }

    void setSampleRate(double sampleRate) override {
        if (sampleRate > 0.0) {
            m_sampleRate = sampleRate;
        }
    }

    std::unique_ptr<IPlugin> instantiatePlugin(const PluginDescriptor& descriptor) override {
        // Phase 3: Plugin Instantiation (The Factory)
        if (descriptor.formatFlags & PluginFormatFlags::BUILTIN) {
            const bool drums = std::strcmp(descriptor.filePath, GMSynthPlugin::kDrumsPath) == 0;
            return std::make_unique<GMSynthPlugin>(drums ? GMSynthPlugin::Mode::Drums : GMSynthPlugin::Mode::Melodic,
                                                   m_sampleRate.load());
        }
        if (descriptor.formatFlags & PluginFormatFlags::VST3) {
            return std::make_unique<VST3PluginImpl>(descriptor.filePath, m_sampleRate.load());
        }
        else if (descriptor.formatFlags & PluginFormatFlags::CLAP) {
            return std::make_unique<CLAPPluginImpl>(descriptor.filePath);
        }
#if defined(__APPLE__)
        else if (descriptor.formatFlags & PluginFormatFlags::AU) {
            return std::make_unique<AUPluginImpl>(descriptor.filePath, m_sampleRate.load());
        }
#endif
        return nullptr;
    }

    bool isScanning() const override {
        return m_scanner ? m_scanner->isScanning() : false;
    }

    float getScanProgress() const override {
        return m_scanner ? m_scanner->getProgress().progress : 0.0f;
    }

    std::string getCurrentlyScanningPlugin() const override {
        return m_scanner ? m_scanner->getProgress().currentPluginName : "";
    }

private:
    std::vector<std::string> getDefaultPaths() {
        std::vector<std::string> paths;
#if defined(__APPLE__)
        paths.push_back("/Library/Audio/Plug-Ins/VST3");
        paths.push_back("/Library/Audio/Plug-Ins/Components");
#elif defined(_WIN32)
        paths.push_back("C:\\Program Files\\Common Files\\VST3");
#endif
        return paths;
    }

    std::unique_ptr<Layer2::IPluginScanner> m_scanner;
    std::vector<PluginDescriptor> m_registry;
    mutable std::mutex m_registryMutex;
    std::atomic<double> m_sampleRate{44100.0};
};

std::unique_ptr<IPluginManager> IPluginManager::create() {
    return std::make_unique<PluginManagerImpl>();
}

} // namespace Layer3
