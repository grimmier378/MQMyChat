#pragma once

#include <chrono>
#include <vector>
#include <utility>

struct ImGuiInputTextCallbackData;
struct ChatChannel;
class MyChatEngine;

class MyChatRenderer
{
public:
    void RenderMainWindow(MyChatEngine& engine);
    void RenderPopOutWindows(MyChatEngine& engine);
    void RenderSettingsWindow(MyChatEngine& engine);
    void RenderPresetManager(MyChatEngine& engine);

private:
    void DrawConsole(MyChatEngine& engine, int channelId);
    void DrawMainConsole(MyChatEngine& engine);
    int  TextEditCallback(ImGuiInputTextCallbackData* data, MyChatEngine& engine, int channelId);
    void DetectTabReorder(MyChatEngine& engine);
    void FlushTabOrder(MyChatEngine& engine);
    bool CheckFocusKey(MyChatEngine& engine) const;

    void DrawChannelList(MyChatEngine& engine);
    void DrawMainSettingsTab(MyChatEngine& engine);
    void DrawChannelSettingsTab(MyChatEngine& engine, ChatChannel& ch);
    void DrawEventsAndFiltersTab(MyChatEngine& engine, ChatChannel& ch);

    std::vector<std::pair<int, float>> m_tabPositions;
    std::vector<int>                   m_lastTabOrder;
    bool                               m_tabOrderDirty = false;
    std::chrono::steady_clock::time_point m_tabOrderLastSave;

    bool m_setFocus = false;

    int  m_selEventIndex = -1;
    int  m_settingsTab = 0;

    char m_presetCopyName[256] = {};
    char m_presetNewName[256] = {};
    char m_presetRenameBuf[256] = {};
    int  m_renamePresetId = -1;
    int  m_deletePresetId = -1;
    bool m_confirmDelete = false;
};
