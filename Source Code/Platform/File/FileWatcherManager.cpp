#include "stdafx.h"

#include "Headers/FileWatcherManager.h"

namespace Divide {
    namespace {
        constexpr U32 g_updateFrameInterval = 60;
    }

    vector<std::pair<FileWatcher_uptr, U32>> FileWatcherManager::s_fileWatchers;

    FileWatcher& FileWatcherManager::allocateWatcher() {
        s_fileWatchers.emplace_back(std::make_pair(eastl::make_unique<FileWatcher>(), g_updateFrameInterval));
        s_fileWatchers.back().first->_impl = eastl::make_unique<FW::FileWatcher>();
        return *s_fileWatchers.back().first;
    }

    void FileWatcherManager::deallocateWatcher(const FileWatcher& fw) {
        deallocateWatcher(fw.getGUID());
    }

    void FileWatcherManager::deallocateWatcher(I64 fileWatcherGUID) {
        s_fileWatchers.erase(
            std::remove_if(std::begin(s_fileWatchers), std::end(s_fileWatchers),
                           [fileWatcherGUID](const auto& it) noexcept
                           -> bool { return it.first->getGUID() == fileWatcherGUID; }),
            std::end(s_fileWatchers));
    }

    void FileWatcherManager::update() {
        // Expensive: update just once every few frame
        for (auto& [fw, counter] : s_fileWatchers) {
            if (--counter == 0u) {
                counter = g_updateFrameInterval;
                (*fw)().update();
                return;
            }
        }
    }
}; //namespace Divide