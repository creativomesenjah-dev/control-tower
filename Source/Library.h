// Library.h — biblioteca de temas: carpetas elegidas por el usuario, escaneo en segundo plano,
// búsqueda y una selección compartida entre la pantalla y el pote BROWSE de la FLX4.
#pragma once

#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <vector>

namespace ct
{

class Library : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    struct Folder
    {
        juce::String name;
        juce::File dir;
        juce::Array<juce::File> tracks;
    };

    Library() : juce::Thread ("Control Tower: biblioteca") {}
    ~Library() override { *alive = false; stopThread (4000); }

    static bool isAudioFile (const juce::File& f)
    {
        static const juce::StringArray ext { ".mp3", ".wav", ".aif", ".aiff", ".flac", ".m4a", ".aac", ".ogg" };
        return ext.contains (f.getFileExtension().toLowerCase());
    }

    // --- carpetas raíz
    juce::StringArray getRoots() const { return roots; }
    void setRoots (const juce::StringArray& r) { roots = r; rescan(); }
    void addRoot (const juce::File& dir)
    {
        if (! roots.contains (dir.getFullPathName())) roots.add (dir.getFullPathName());
        rescan();
    }
    void removeRoot (const juce::String& path) { roots.removeString (path); rescan(); }

    void rescan()
    {
        stopThread (4000);
        scanning = true;
        sendChangeMessage();
        startThread (juce::Thread::Priority::low);
    }
    bool isScanning() const { return scanning; }

    // --- vista
    const std::vector<Folder>& getFolders() const { return folders; }
    int getNumTracksTotal() const { return folders.empty() ? 0 : folders[0].tracks.size(); }

    void setSearch (const juce::String& text)
    {
        search = text.trim();
        rebuildVisible();
        selectedTrack = visible.isEmpty() ? -1 : 0;
        sendChangeMessage();
    }
    juce::String getSearch() const { return search; }

    const juce::Array<juce::File>& getVisibleTracks() const { return visible; }

    int  getSelectedFolder() const { return selectedFolder; }
    int  getSelectedTrack() const  { return selectedTrack; }
    bool isTrackFocus() const      { return trackFocus; }

    void selectFolder (int i)
    {
        if (folders.empty()) return;
        selectedFolder = juce::jlimit (0, (int) folders.size() - 1, i);
        rebuildVisible();
        selectedTrack = visible.isEmpty() ? -1 : 0;
        sendChangeMessage();
    }

    void selectTrack (int i)
    {
        selectedTrack = visible.isEmpty() ? -1 : juce::jlimit (0, visible.size() - 1, i);
        sendChangeMessage();
    }

    void setTrackFocus (bool tracks) { trackFocus = tracks; sendChangeMessage(); }
    void toggleFocus() { setTrackFocus (! trackFocus); }

    // pote BROWSE: mueve la selección de la lista con foco
    void moveSelection (int delta)
    {
        if (trackFocus || ! search.isEmpty()) selectTrack (juce::jmax (0, selectedTrack) + delta);
        else selectFolder (selectedFolder + delta);
    }

    juce::File getSelectedTrackFile() const
    {
        return juce::isPositiveAndBelow (selectedTrack, visible.size()) ? visible[selectedTrack] : juce::File();
    }

private:
    void run() override
    {
        std::vector<Folder> result;
        Folder all;
        all.name = u8str ("TODOS LOS TEMAS");
        std::map<juce::String, size_t> byDir;

        for (const auto& rootPath : roots)
        {
            const juce::File root (rootPath);
            if (! root.isDirectory()) continue;
            for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
            {
                if (threadShouldExit()) return;
                const auto f = entry.getFile();
                if (! isAudioFile (f) || f.getFileName().startsWithChar ('.')) continue;
                const auto dir = f.getParentDirectory();
                auto it = byDir.find (dir.getFullPathName());
                if (it == byDir.end())
                {
                    Folder fo;
                    fo.dir = dir;
                    fo.name = dir == root ? root.getFileName()
                                          : root.getFileName() + " / " + dir.getRelativePathFrom (root).replace ("/", " / ");
                    result.push_back (fo);
                    it = byDir.emplace (dir.getFullPathName(), result.size() - 1).first;
                }
                result[it->second].tracks.add (f);
                all.tracks.add (f);
            }
        }

        auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
        for (auto& fo : result) std::sort (fo.tracks.begin(), fo.tracks.end(), byName);
        std::sort (result.begin(), result.end(), [] (const Folder& a, const Folder& b) { return a.name.compareNatural (b.name) < 0; });
        std::sort (all.tracks.begin(), all.tracks.end(), byName);
        result.insert (result.begin(), all);

        auto flag = alive;
        auto shared = std::make_shared<std::vector<Folder>> (std::move (result));
        juce::MessageManager::callAsync ([this, flag, shared]
        {
            if (! *flag) return;
            const auto previous = selectedFolder < (int) folders.size() ? folders[(size_t) selectedFolder].dir : juce::File();
            folders = std::move (*shared);
            selectedFolder = 0;
            for (size_t i = 1; i < folders.size(); ++i) if (folders[i].dir == previous) selectedFolder = (int) i;
            scanning = false;
            rebuildVisible();
            selectedTrack = visible.isEmpty() ? -1 : juce::jmin (juce::jmax (0, selectedTrack), visible.size() - 1);
            sendChangeMessage();
        });
    }

    static juce::String u8str (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    void rebuildVisible()
    {
        visible.clearQuick();
        if (folders.empty()) return;
        if (search.isNotEmpty())
        {
            juce::StringArray words;
            words.addTokens (search, " ", "");
            words.removeEmptyStrings();
            for (const auto& f : folders[0].tracks)
            {
                const auto name = f.getFileNameWithoutExtension();
                bool ok = true;
                for (const auto& w : words) if (! name.containsIgnoreCase (w)) { ok = false; break; }
                if (ok) visible.add (f);
            }
        }
        else
        {
            visible = folders[(size_t) juce::jlimit (0, (int) folders.size() - 1, selectedFolder)].tracks;
        }
    }

    juce::StringArray roots;
    std::vector<Folder> folders;
    juce::Array<juce::File> visible;
    juce::String search;
    int selectedFolder = 0, selectedTrack = -1;
    bool trackFocus = true, scanning = false;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace ct
