#include "MyChatRenderer.h"
#include "MQMyChat.h"
#include "MyChatDatabase.h"
#include "Theme.h"
#include "Core/Widgets.h"

#include <mq/Plugin.h>
#include <zep/display.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/fonts/IconsMaterialDesign.h>

#include "imgui/ImGuiUtils.h"

#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <unordered_map>

static ImGuiKey GetImGuiKeyByName(const std::string& name)
{
    static const std::unordered_map<std::string, ImGuiKey> keyMap = {
        {"GraveAccent", ImGuiKey_GraveAccent},
        {"Enter", ImGuiKey_Enter},
        {"RightShift", ImGuiKey_RightShift},
        {"LeftShift", ImGuiKey_LeftShift},
        {"Tab", ImGuiKey_Tab},
        {"LeftArrow", ImGuiKey_LeftArrow},
        {"RightArrow", ImGuiKey_RightArrow},
        {"UpArrow", ImGuiKey_UpArrow},
        {"DownArrow", ImGuiKey_DownArrow},
        {"RightCtrl", ImGuiKey_RightCtrl},
        {"LeftCtrl", ImGuiKey_LeftCtrl},
        {"RightAlt", ImGuiKey_RightAlt},
        {"LeftAlt", ImGuiKey_LeftAlt},
        {"Space", ImGuiKey_Space},
        {"Backspace", ImGuiKey_Backspace},
        {"Delete", ImGuiKey_Delete},
        {"Insert", ImGuiKey_Insert},
        {"Home", ImGuiKey_Home},
        {"End", ImGuiKey_End},
        {"PageUp", ImGuiKey_PageUp},
        {"PageDown", ImGuiKey_PageDown},
        {"F1", ImGuiKey_F1}, {"F2", ImGuiKey_F2}, {"F3", ImGuiKey_F3},
        {"F4", ImGuiKey_F4}, {"F5", ImGuiKey_F5}, {"F6", ImGuiKey_F6},
        {"F7", ImGuiKey_F7}, {"F8", ImGuiKey_F8}, {"F9", ImGuiKey_F9},
        {"F10", ImGuiKey_F10}, {"F11", ImGuiKey_F11}, {"F12", ImGuiKey_F12},
    };

    auto it = keyMap.find(name);
    if (it != keyMap.end())
        return it->second;
    return ImGuiKey_None;
}

bool MyChatRenderer::CheckFocusKey(MyChatEngine& engine) const
{
    if (!engine.settings.keyFocus)
        return false;

    ImGuiKey key = GetImGuiKeyByName(engine.settings.keyName);
    if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, false))
        return true;

    return false;
}

void MyChatRenderer::RenderMainWindow(MyChatEngine& engine)
{
    if (!engine.showMainWindow)
        return;

    auto oldStyle = ImGuiTheme::ApplyTheme(engine.settings.themeIdx);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;
    if (engine.settings.windowLocked)
        windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

    std::string winName = fmt::format("My Chat - Main##MQMyChat_{}", engine.charName);
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(winName.c_str(), &engine.showMainWindow, windowFlags))
    {
        ImGui::End();
        ImGuiTheme::ResetTheme(oldStyle);
        return;
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::SmallButton(engine.settings.windowLocked ? ICON_MD_LOCK : ICON_MD_LOCK_OPEN))
            engine.settings.windowLocked = !engine.settings.windowLocked;

        if (ImGui::SmallButton(ICON_MD_SETTINGS))
            engine.showSettingsWindow = !engine.showSettingsWindow;

        if (ImGui::BeginMenu("Options"))
        {
            myui::StyledCheckbox("Timestamps", &engine.settings.timeStamps);
            myui::StyledCheckbox("Local Echo", &engine.settings.localEcho);
            myui::StyledCheckbox("Auto Scroll", &engine.settings.autoScroll);
            myui::StyledCheckbox("Log Commands", &engine.settings.logCommands);
            if (myui::StyledCheckbox("Spam Channel", &engine.enableSpam))
            {
                if (engine.enableSpam)
                {
                    auto spamIt = engine.settings.channels.find(CHANNEL_SPAM);
                    if (spamIt == engine.settings.channels.end())
                        engine.CreateChannel("Spam", CHANNEL_SPAM);
                    else
                        spamIt->second.enabled = true;
                }
                else
                {
                    auto spamIt = engine.settings.channels.find(CHANNEL_SPAM);
                    if (spamIt != engine.settings.channels.end())
                        spamIt->second.enabled = false;
                }
                engine.SortChannels();
            }
            ImGui::Separator();
            myui::StyledCheckbox("Key Focus", &engine.settings.keyFocus);
            if (engine.settings.keyFocus)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                char keyBuf[64] = {};
                strncpy_s(keyBuf, engine.settings.keyName.c_str(), sizeof(keyBuf) - 1);
                if (ImGui::InputText("##KeyName", keyBuf, sizeof(keyBuf)))
                {
                    engine.settings.keyName = keyBuf;
                }
            }
            ImGui::Separator();
            ImGui::Text("Max Buffer Lines:");
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("##MaxBufferLines", &engine.settings.maxBufferLines))
            {
                if (engine.settings.maxBufferLines < 500) engine.settings.maxBufferLines = 500;
                if (engine.settings.maxBufferLines > 50000) engine.settings.maxBufferLines = 50000;
                engine.ApplyBufferSize();
            }
            ImGui::Separator();
            ImGui::Text("Theme:");
            engine.settings.themeIdx = ImGuiTheme::DrawThemePicker(engine.settings.themeIdx, "MyChatTheme");
            ImGui::Separator();
            if (ImGui::Button("Save Settings"))
            {
                engine.SaveCharacterSettings();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Channels"))
        {
            for (auto& [id, ch] : engine.settings.channels)
            {
                if (id == CHANNEL_SPAM)
                {
                    continue;
                }
                if (myui::StyledCheckbox(ch.name.c_str(), &ch.enabled))
                {
                    engine.RefreshBlechEvents();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Links"))
        {
            for (auto& [id, ch] : engine.settings.channels)
            {
                myui::StyledCheckbox(fmt::format("{}##links", ch.name).c_str(), &ch.enableLinks);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("PopOut"))
        {
            for (auto& [id, ch] : engine.settings.channels)
            {
                myui::StyledCheckbox(fmt::format("{}##popout", ch.name).c_str(), &ch.popOut);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Presets"))
        {
            ImGui::Text("Preset:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            if (myui::StyledBeginCombo("##PresetCombo", engine.activePresetName.c_str()))
            {
                for (int i = 0; i < static_cast<int>(engine.presetList.size()); i++)
                {
                    bool isSelected = (engine.presetList[i].id == engine.activePresetId);
                    // Fixed pill width: the combo popup auto-sizes, so PillSelectable
                    // needs an explicit width or it collapses to a sliver.
                    if (myui::PillSelectable(engine.presetList[i].name.c_str(), isSelected, 150.0f))
                    {
                        if (!isSelected)
                        {
                            engine.SaveCharacterSettings();
                            engine.database->SetActivePreset(engine.serverName, engine.charName, engine.presetList[i].id);
                            engine.UnloadCharacterSettings();
                            engine.LoadCharacterSettings();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                myui::StyledEndCombo();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Manage Presets..."))
            {
                engine.showPresetManager = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_Reorderable
        | ImGuiTabBarFlags_FittingPolicyShrink
        | ImGuiTabBarFlags_TabListPopupButton;

    m_tabPositions.clear();

    if (ImGui::BeginTabBar("##ChatTabs", tabFlags))
    {
        if (ImGui::BeginTabItem("Main"))
        {
            DrawMainConsole(engine);
            ImGui::EndTabItem();
        }

        for (auto& sc : engine.sortedChannels)
        {
            auto it = engine.settings.channels.find(sc.id);
            if (it == engine.settings.channels.end() || !it->second.enabled) continue;

            if (it->second.popOut) continue;

            std::string tabName = fmt::format("{}##{}_{}", sc.name, sc.id, engine.activePresetId);
            bool selected = ImGui::BeginTabItem(tabName.c_str());
            float tabX = ImGui::GetItemRectMin().x;
            m_tabPositions.push_back({ sc.id, tabX });

            if (selected)
            {
                DrawConsole(engine, sc.id);
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    DetectTabReorder(engine);

    auto now = std::chrono::steady_clock::now();
    if (m_tabOrderDirty)
    {
        float elapsed = std::chrono::duration<float>(now - m_tabOrderLastSave).count();
        if (elapsed >= TAB_ORDER_SAVE_INTERVAL)
        {
            FlushTabOrder(engine);
            m_tabOrderLastSave = now;
            m_tabOrderDirty = false;
        }
    }

    ImGui::End();
    ImGuiTheme::ResetTheme(oldStyle);
}

void MyChatRenderer::RenderPopOutWindows(MyChatEngine& engine)
{
    auto oldStyle = ImGuiTheme::ApplyTheme(engine.settings.themeIdx);

    for (auto& [id, ch] : engine.settings.channels)
    {
        if (!ch.popOut || !ch.enabled) continue;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
        if (ch.locked)
        {
            windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        }

        std::string winName = fmt::format("My Chat - {}##MQMyChat_{}{}", ch.name, engine.charName, id);
        bool open = ch.popOut;
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(winName.c_str(), &open, windowFlags))
        {
            if (ImGui::SmallButton(ch.locked ? ICON_MD_LOCK : ICON_MD_LOCK_OPEN))
            {
                ch.locked = !ch.locked;
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_SETTINGS))
            {
                engine.settingsChannelId = id;
                engine.showSettingsWindow = true;
            }

            DrawConsole(engine, id);
        }
        ImGui::End();

        if (!open) ch.popOut = false;
    }

    ImGuiTheme::ResetTheme(oldStyle);
}

bool MyChatRenderer::PopupTextEdit(const char* id, std::string& value, float width)
{
    bool changed = false;
    ImGui::PushID(id);

    std::string shown = value.empty() ? "<empty>" : value;
    ImVec2 btnSize(width > 0.0f ? width : 0.0f, 0.0f);
    if (ImGui::Button(shown.c_str(), btnSize))
    {
        strncpy_s(m_popupBuf, value.c_str(), sizeof(m_popupBuf) - 1);
        ImGui::SetNextWindowPos(ImGui::GetMousePos());
        ImGui::OpenPopup("##popupedit");
        m_popupFocus = true;
    }

    if (ImGui::BeginPopup("##popupedit"))
    {
        float fitWidth = ImGui::CalcTextSize(m_popupBuf).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f;
        fitWidth = ImClamp(fitWidth, 120.0f, 600.0f);
        ImGui::SetNextItemWidth(fitWidth);
        if (m_popupFocus)
        {
            ImGui::SetKeyboardFocusHere();
            m_popupFocus = false;
        }
        bool entered = ImGui::InputText("##popupbuf", m_popupBuf, sizeof(m_popupBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::Button("Accept") || entered)
        {
            value = m_popupBuf;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

void MyChatRenderer::DrawChannelList(MyChatEngine& engine)
{
    if (myui::PillSelectable("Main", engine.settingsChannelId == CHANNEL_MAIN_ID))
    {
        engine.settingsChannelId = CHANNEL_MAIN_ID;
        m_selEventIndex = -1;
    }

    for (auto& [id, ch] : engine.settings.channels)
    {
        if (id == CHANNEL_SPAM)
            continue;

        ImGui::PushID(id);
        if (myui::PillSelectable(ch.name.c_str(), engine.settingsChannelId == id))
        {
            engine.settingsChannelId = id;
            m_selEventIndex = -1;
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add", ImVec2(-1.0f, 0.0f)))
    {
        m_popupBuf[0] = '\0';
        ImGui::SetNextWindowPos(ImGui::GetMousePos());
        ImGui::OpenPopup("##AddChannelPopup");
        m_popupFocus = true;
    }

    if (ImGui::BeginPopup("##AddChannelPopup"))
    {
        ImGui::SetNextItemWidth(200.0f);
        if (m_popupFocus)
        {
            ImGui::SetKeyboardFocusHere();
            m_popupFocus = false;
        }
        bool entered = ImGui::InputText("##addchname", m_popupBuf, sizeof(m_popupBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        bool accept = ImGui::Button("Accept") || entered;
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        if (accept && m_popupBuf[0] != '\0')
        {
            int newId = engine.GetNextChannelId();
            engine.CreateChannel(m_popupBuf, newId);
            engine.SortChannels();
            engine.settingsChannelId = newId;
            m_selEventIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MyChatRenderer::DrawMainSettingsTab(MyChatEngine& engine)
{
    ImGui::TextUnformatted("Main Echo:");
    ImGui::SameLine();
    PopupTextEdit("##MainEcho", engine.settings.mainEcho, 200.0f);

    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Main Font Size", &engine.settings.mainFontSize))
    {
        if (engine.settings.mainFontSize < 8) engine.settings.mainFontSize = 8;
        if (engine.settings.mainFontSize > 48) engine.settings.mainFontSize = 48;
        engine.ApplyFontSizes();
    }

    myui::StyledCheckbox("Links", &engine.settings.doLinks);
}

void MyChatRenderer::DrawChannelSettingsTab(MyChatEngine& engine, ChatChannel& ch)
{
    ImGui::Text("Channel ID: %d", ch.channelId);
    ImGui::Separator();

    ImGui::TextUnformatted("Name:");
    ImGui::SameLine();
    if (PopupTextEdit("##ChName", ch.name, 200.0f))
        engine.SortChannels();

    ImGui::TextUnformatted("Echo:");
    ImGui::SameLine();
    PopupTextEdit("##ChEcho", ch.echo, 200.0f);

    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Font Size", &ch.fontSize))
    {
        if (ch.fontSize < 8) ch.fontSize = 8;
        if (ch.fontSize > 48) ch.fontSize = 48;
        if (ch.console)
        {
            auto& font = ch.console->GetEditor().GetDisplay().GetFont(Zep::ZepTextType::Text);
            font.SetPixelHeight(ch.fontSize);
        }
    }

    ImGui::Separator();
    if (myui::StyledCheckbox("Enabled", &ch.enabled))
        engine.RefreshBlechEvents();
    myui::StyledCheckbox("Main Enable", &ch.mainEnable);
    myui::StyledCheckbox("Enable Links", &ch.enableLinks);
    myui::StyledCheckbox("PopOut", &ch.popOut);
    myui::StyledCheckbox("Locked", &ch.locked);

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    bool removed = ImGui::Button("Remove Channel");
    ImGui::PopStyleColor();
    if (removed)
    {
        engine.settings.channels.erase(ch.channelId);
        engine.SortChannels();
        engine.settingsChannelId = CHANNEL_MAIN_ID;
        m_selEventIndex = -1;
    }
}

void MyChatRenderer::DrawEventsAndFiltersTab(MyChatEngine& engine, ChatChannel& ch)
{
    const float listWidth = 200.0f;
    int eventToRemove = -1;

    ImGui::BeginChild("##EventListPane", ImVec2(listWidth, 0.0f), true);

    if (ImGui::Button("+ Add Event", ImVec2(-1.0f, 0.0f)))
    {
        m_popupBuf[0] = '\0';
        ImGui::SetNextWindowPos(ImGui::GetMousePos());
        ImGui::OpenPopup("##AddEventPopup");
        m_popupFocus = true;
    }

    if (ImGui::BeginPopup("##AddEventPopup"))
    {
        ImGui::SetNextItemWidth(220.0f);
        if (m_popupFocus)
        {
            ImGui::SetKeyboardFocusHere();
            m_popupFocus = false;
        }
        bool entered = ImGui::InputText("##addevt", m_popupBuf, sizeof(m_popupBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        bool accept = ImGui::Button("Accept") || entered;
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        if (accept && m_popupBuf[0] != '\0')
        {
            int newEventIndex = 0;
            for (const auto& existing : ch.events)
            {
                if (existing.eventIndex >= newEventIndex)
                    newEventIndex = existing.eventIndex + 1;
            }
            ChatEvent newEvent;
            newEvent.eventIndex = newEventIndex;
            newEvent.eventString = m_popupBuf;
            ch.events.push_back(newEvent);
            m_selEventIndex = static_cast<int>(ch.events.size()) - 1;
            engine.RefreshBlechEvents();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    for (int ei = 0; ei < static_cast<int>(ch.events.size()); ei++)
    {
        auto& evt = ch.events[ei];
        ImGui::PushID(ei);
        std::string label = evt.eventString.empty()
            ? std::string("<new event>")
            : evt.eventString.substr(0, 24);
        if (myui::PillSelectable(label.c_str(), m_selEventIndex == ei))
            m_selEventIndex = ei;
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##EventDetailPane", ImVec2(0.0f, 0.0f), true);
    if (m_selEventIndex < 0 || m_selEventIndex >= static_cast<int>(ch.events.size()))
    {
        ImGui::TextDisabled("Select an event from the list, or add a new one.");
    }
    else
    {
        auto& evt = ch.events[m_selEventIndex];

        if (evt.filters.empty())
        {
            ChatFilter defaultFilter;
            defaultFilter.filterIndex = 0;
            defaultFilter.color = MQColor(240, 240, 240);
            evt.filters.insert(evt.filters.begin(), defaultFilter);
        }

        if (myui::StyledCheckbox("Enabled##evt", &evt.enabled))
            engine.RefreshBlechEvents();
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove Event"))
            eventToRemove = m_selEventIndex;

        ImGui::TextUnformatted("Pattern:");
        ImGui::SameLine();
        if (PopupTextEdit("##evtPattern", evt.eventString, -1.0f))
            engine.RefreshBlechEvents();

        auto& defaultFilt = evt.filters[0];
        ImVec4 evtCol = defaultFilt.color.ToImColor();
        ImGui::Text("Event Color:");
        ImGui::SameLine();
        if (ImGui::ColorEdit4("##evtColor", &evtCol.x, ImGuiColorEditFlags_NoInputs))
            defaultFilt.color = MQColor(evtCol);

        ImGui::Separator();
        if (evt.filters.size() > 1)
            ImGui::TextUnformatted("Filters:");

        int filterToRemove = -1;
        int filterSwapFrom = -1, filterSwapTo = -1;
        for (int fi = 1; fi < static_cast<int>(evt.filters.size()); fi++)
        {
            auto& flt = evt.filters[fi];
            ImGui::PushID(fi);

            PopupTextEdit("##fltPattern", flt.filterString, 200.0f);

            ImGui::SameLine();
            ImVec4 col = flt.color.ToImColor();
            if (ImGui::ColorEdit4("##fltColor", &col.x, ImGuiColorEditFlags_NoInputs))
                flt.color = MQColor(col);

            ImGui::SameLine();
            myui::StyledCheckbox("On##flt", &flt.enabled);

            ImGui::SameLine();
            myui::StyledCheckbox("Hidden##flt", &flt.hidden);

            ImGui::SameLine();
            if (ImGui::SmallButton("X##flt"))
                filterToRemove = fi;

            if (fi > 1)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_ARROW_UPWARD "##flt"))
                {
                    filterSwapFrom = fi;
                    filterSwapTo = fi - 1;
                }
            }

            if (fi + 1 < static_cast<int>(evt.filters.size()))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MD_ARROW_DOWNWARD "##flt"))
                {
                    filterSwapFrom = fi;
                    filterSwapTo = fi + 1;
                }
            }

            ImGui::PopID();
        }

        if (filterSwapFrom >= 1 && filterSwapTo >= 1)
            std::swap(evt.filters[filterSwapFrom], evt.filters[filterSwapTo]);

        if (filterToRemove >= 1)
            evt.filters.erase(evt.filters.begin() + filterToRemove);

        if (ImGui::SmallButton("Add Filter"))
            evt.filters.push_back(ChatFilter{});
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("You can add TOKENs to your filters in place of character names.");
            ImGui::Separator();
            ImGui::Text("LIST OF TOKENS");
            ImGui::BulletText("M3        Your Name");
            ImGui::BulletText("M1        Main Assist Name");
            ImGui::BulletText("PT1       Your Pet Name");
            ImGui::BulletText("PT3       Any Group Member's Pet Name");
            ImGui::BulletText("GP1       Party Member's Name");
            ImGui::BulletText("TK1       Main Tank Name");
            ImGui::BulletText("RL        Raid Leader Name");
            ImGui::BulletText("H1        Group Healer (DRU, CLR, or SHM)");
            ImGui::BulletText("G1 - G5   Party Member in Group Slot 1-5");
            ImGui::BulletText("N3        NPC Name");
            ImGui::BulletText("P3        PC Name");
            ImGui::Separator();
            ImGui::Text("PREFIX MODIFIERS");
            ImGui::BulletText("NO2       Invert match. Place before a token or word\n          to exclude lines that match.");
            ImGui::BulletText("^         Anchor to start of line.");
            ImGui::Separator();
            ImGui::TextDisabled("Lua patterns supported: %%d %%a %%] etc.");
            ImGui::EndTooltip();
        }

        for (int idx = 0; idx < static_cast<int>(evt.filters.size()); idx++)
            evt.filters[idx].filterIndex = idx;
    }
    ImGui::EndChild();

    if (eventToRemove >= 0)
    {
        ch.events.erase(ch.events.begin() + eventToRemove);
        if (m_selEventIndex >= static_cast<int>(ch.events.size()))
            m_selEventIndex = static_cast<int>(ch.events.size()) - 1;
        engine.RefreshBlechEvents();
    }
}

void MyChatRenderer::RenderSettingsWindow(MyChatEngine& engine)
{
    if (!engine.showSettingsWindow)
        return;

    auto oldStyle = ImGuiTheme::ApplyTheme(engine.settings.themeIdx);

    std::string winName = fmt::format("My Chat - Settings##MQMyChat_{}", engine.charName);
    ImGui::SetNextWindowSize(ImVec2(720, 460), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(winName.c_str(), &engine.showSettingsWindow, ImGuiWindowFlags_None))
    {
        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        const float listWidth = 180.0f;

        ImGui::BeginChild("##ChannelListPane", ImVec2(listWidth, -footerHeight), true);
        DrawChannelList(engine);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##ChannelDetailPane", ImVec2(0.0f, -footerHeight), true);
        if (engine.settingsChannelId == CHANNEL_MAIN_ID)
        {
            const char* mainTabs[] = { "Settings" };
            myui::PillTabBar("##MainTabs", mainTabs, 1, 0);
            ImGui::Spacing();
            DrawMainSettingsTab(engine);
        }
        else
        {
            auto it = engine.settings.channels.find(engine.settingsChannelId);
            if (it == engine.settings.channels.end())
            {
                ImGui::TextDisabled("Select a channel from the list.");
            }
            else
            {
                ChatChannel& ch = it->second;
                const char* chTabs[] = { "Settings", "Events and Filters" };
                m_settingsTab = myui::PillTabBar("##SettingsTabs", chTabs, 2, m_settingsTab);
                ImGui::Spacing();
                if (m_settingsTab == 1)
                    DrawEventsAndFiltersTab(engine, ch);
                else
                    DrawChannelSettingsTab(engine, ch);
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Save"))
        {
            engine.UnregisterBlechEvents();
            engine.RegisterBlechEvents();
            engine.SortChannels();
            engine.ApplyFontSizes();
            engine.database->SaveSettings(engine.activePresetId, engine.settings);
            engine.SaveCharacterSettings();
            engine.showSettingsWindow = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
            engine.showSettingsWindow = false;
    }
    ImGui::End();

    ImGuiTheme::ResetTheme(oldStyle);
}

void MyChatRenderer::DrawConsole(MyChatEngine& engine, int channelId)
{
    auto it = engine.settings.channels.find(channelId);
    if (it == engine.settings.channels.end())
        return;

    auto& ch = it->second;
    if (!ch.console)
        return;

    const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    contentSize.y -= footerHeight;

    ch.console->Render(ImVec2(0, contentSize.y));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 4));
    ImGui::Separator();

    int textFlags = ImGuiInputTextFlags_EnterReturnsTrue
        | ImGuiInputTextFlags_CallbackHistory;

    std::string inputLabel = fmt::format("##Input{}", channelId);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
    ImGui::PushFont(mq::imgui::ConsoleFont);

    struct CallbackUserData {
        MyChatRenderer* renderer;
        MyChatEngine*   engine;
        int             channelId;
    };

    CallbackUserData cbData{ this, &engine, channelId };

    bool bTextEdit = ImGui::InputText(inputLabel.c_str(), ch.inputBuffer, IM_ARRAYSIZE(ch.inputBuffer), textFlags,
        [](ImGuiInputTextCallbackData* data) -> int
        {
            auto* ud = static_cast<CallbackUserData*>(data->UserData);
            return ud->renderer->TextEditCallback(data, *ud->engine, ud->channelId);
        }, &cbData);

    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (bTextEdit)
    {
        std::string trimmed = trim_copy(std::string(ch.inputBuffer));
        const char* s = trimmed.c_str();

        if (s[0])
        {
            if (engine.settings.localEcho && ch.console)
                engine.AppendToConsole(ch.console, fmt::format("> {}", s), MQColor(128, 128, 128));

            for (int i = static_cast<int>(ch.commandHistory.size()) - 1; i >= 0; --i)
            {
                if (ci_equals(ch.commandHistory[i], s))
                {
                    ch.commandHistory.erase(ch.commandHistory.begin() + i);
                    break;
                }
            }
            ch.commandHistory.emplace_back(s);
            if (static_cast<int>(ch.commandHistory.size()) > MAX_COMMAND_HISTORY)
                ch.commandHistory.erase(ch.commandHistory.begin());

            if (s[0] == '/')
            {
                DoCommand(s, true);
            }
            else
            {
                std::string cmd = fmt::format("{} {}", ch.echo, s);
                DoCommand(cmd.c_str(), true);
            }
        }

        memset(ch.inputBuffer, 0, sizeof(ch.inputBuffer));
        ch.historyPos = -1;
        m_setFocus = true;
    }

    ImGui::SetItemDefaultFocus();
    if (CheckFocusKey(engine))
        m_setFocus = true;

    if (m_setFocus)
    {
        m_setFocus = false;
        ImGui::SetKeyboardFocusHere(-1);
    }
}

void MyChatRenderer::DrawMainConsole(MyChatEngine& engine)
{
    if (!engine.mainConsole)
        return;

    const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    contentSize.y -= footerHeight;

    engine.mainConsole->Render(ImVec2(0, contentSize.y));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 4));
    ImGui::Separator();

    int textFlags = ImGuiInputTextFlags_EnterReturnsTrue
        | ImGuiInputTextFlags_CallbackHistory;

    static char mainInputBuffer[2048] = {};
    static std::vector<std::string> mainHistory;
    static int mainHistoryPos = -1;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
    ImGui::PushFont(mq::imgui::ConsoleFont);

    struct MainCallbackData {
        std::vector<std::string>* history;
        int* historyPos;
    };

    MainCallbackData mcbData{ &mainHistory, &mainHistoryPos };

    bool bTextEdit = ImGui::InputText("##InputMain", mainInputBuffer, IM_ARRAYSIZE(mainInputBuffer), textFlags,
        [](ImGuiInputTextCallbackData* data) -> int
        {
            auto* ud = static_cast<MainCallbackData*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
            {
                const int prevPos = *ud->historyPos;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (*ud->historyPos == -1)
                        *ud->historyPos = static_cast<int>(ud->history->size()) - 1;
                    else if (*ud->historyPos > 0)
                        (*ud->historyPos)--;
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (*ud->historyPos != -1)
                        if (++(*ud->historyPos) >= static_cast<int>(ud->history->size()))
                            *ud->historyPos = -1;
                }

                if (prevPos != *ud->historyPos)
                {
                    const char* histStr = (*ud->historyPos >= 0) ? (*ud->history)[*ud->historyPos].c_str() : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, histStr);
                }
            }
            return 0;
        }, &mcbData);

    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (bTextEdit)
    {
        std::string trimmed = trim_copy(std::string(mainInputBuffer));
        const char* s = trimmed.c_str();

        if (s[0])
        {
            if (engine.settings.localEcho && engine.mainConsole)
                engine.AppendToConsole(engine.mainConsole, fmt::format("> {}", s), MQColor(128, 128, 128));

            for (int i = static_cast<int>(mainHistory.size()) - 1; i >= 0; --i)
            {
                if (ci_equals(mainHistory[i], s))
                {
                    mainHistory.erase(mainHistory.begin() + i);
                    break;
                }
            }
            mainHistory.emplace_back(s);
            if (static_cast<int>(mainHistory.size()) > MAX_COMMAND_HISTORY)
                mainHistory.erase(mainHistory.begin());

            if (s[0] == '/')
            {
                DoCommand(s, true);
            }
            else
            {
                std::string cmd = fmt::format("{} {}", engine.settings.mainEcho, s);
                DoCommand(cmd.c_str(), true);
            }
        }

        memset(mainInputBuffer, 0, sizeof(mainInputBuffer));
        mainHistoryPos = -1;
        m_setFocus = true;
    }

    ImGui::SetItemDefaultFocus();
    if (CheckFocusKey(engine))
        m_setFocus = true;

    if (m_setFocus)
    {
        m_setFocus = false;
        ImGui::SetKeyboardFocusHere(-1);
    }
}

int MyChatRenderer::TextEditCallback(ImGuiInputTextCallbackData* data, MyChatEngine& engine, int channelId)
{
    auto it = engine.settings.channels.find(channelId);
    if (it == engine.settings.channels.end())
        return 0;

    auto& ch = it->second;

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        const int prevPos = ch.historyPos;
        if (data->EventKey == ImGuiKey_UpArrow)
        {
            if (ch.historyPos == -1)
                ch.historyPos = static_cast<int>(ch.commandHistory.size()) - 1;
            else if (ch.historyPos > 0)
                ch.historyPos--;
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
            if (ch.historyPos != -1)
                if (++ch.historyPos >= static_cast<int>(ch.commandHistory.size()))
                    ch.historyPos = -1;
        }

        if (prevPos != ch.historyPos)
        {
            const char* histStr = (ch.historyPos >= 0) ? ch.commandHistory[ch.historyPos].c_str() : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, histStr);
        }
    }

    return 0;
}

void MyChatRenderer::DetectTabReorder(MyChatEngine& engine)
{
    if (m_tabPositions.empty())
        return;

    std::sort(m_tabPositions.begin(), m_tabPositions.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<int> currentOrder;
    currentOrder.reserve(m_tabPositions.size());
    for (auto& [id, x] : m_tabPositions)
        currentOrder.push_back(id);

    if (currentOrder != m_lastTabOrder)
    {
        m_lastTabOrder = currentOrder;
        m_tabOrderDirty = true;
        m_tabOrderLastSave = std::chrono::steady_clock::now();
    }
}

void MyChatRenderer::FlushTabOrder(MyChatEngine& engine)
{
    for (int i = 0; i < static_cast<int>(m_lastTabOrder.size()); i++)
    {
        int channelId = m_lastTabOrder[i];
        auto it = engine.settings.channels.find(channelId);
        if (it != engine.settings.channels.end())
            it->second.tabOrder = i;
    }

    engine.SortChannels();
    engine.SaveCharacterSettings();
}

void MyChatRenderer::RenderPresetManager(MyChatEngine& engine)
{
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Preset Manager##MyChat", &engine.showPresetManager))
    {
        ImGui::End();
        return;
    }

    if (myui::BeginAnimatedHeader("Copy Current Preset", true))
    {
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##CopyName", m_presetCopyName, sizeof(m_presetCopyName));
        ImGui::SameLine();
        if (ImGui::Button("Copy") && m_presetCopyName[0] != '\0')
        {
            engine.SaveCharacterSettings();
            engine.database->SaveSettings(engine.activePresetId, engine.settings);
            engine.database->CopyPreset(engine.activePresetId, m_presetCopyName);
            engine.database->GetPresetList(engine.serverName, engine.presetList);
            memset(m_presetCopyName, 0, sizeof(m_presetCopyName));
        }
        myui::EndAnimatedHeader();
    }

    if (myui::BeginAnimatedHeader("Create New Blank Preset", true))
    {
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##NewBlankName", m_presetNewName, sizeof(m_presetNewName));
        ImGui::SameLine();
        if (ImGui::Button("Create") && m_presetNewName[0] != '\0')
        {
            int newId = engine.database->CreateBlankPreset(engine.serverName, engine.charName, m_presetNewName);
            if (newId > 0)
                engine.database->GetPresetList(engine.serverName, engine.presetList);
            memset(m_presetNewName, 0, sizeof(m_presetNewName));
        }
        myui::EndAnimatedHeader();
    }

    if (myui::BeginAnimatedHeader("Rename Preset", true))
    {
        ImGui::SetNextItemWidth(200);
        if (myui::StyledBeginCombo("##RenameCombo", m_renamePresetId > 0
            ? [&]() -> const char* {
                for (auto& p : engine.presetList)
                    if (p.id == m_renamePresetId) return p.name.c_str();
                return "Select...";
            }() : "Select..."))
        {
            for (auto& preset : engine.presetList)
            {
                if (myui::PillSelectable(preset.name.c_str(), preset.id == m_renamePresetId, 200.0f))
                {
                    m_renamePresetId = preset.id;
                    strncpy_s(m_presetRenameBuf, preset.name.c_str(), sizeof(m_presetRenameBuf) - 1);
                    ImGui::CloseCurrentPopup();
                }
            }
            myui::StyledEndCombo();
        }

        if (m_renamePresetId > 0)
        {
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##RenameInput", m_presetRenameBuf, sizeof(m_presetRenameBuf));
            ImGui::SameLine();
            if (ImGui::Button("Rename") && m_presetRenameBuf[0] != '\0')
            {
                engine.database->RenamePreset(m_renamePresetId, m_presetRenameBuf);
                engine.database->GetPresetList(engine.serverName, engine.presetList);
                if (m_renamePresetId == engine.activePresetId)
                    engine.activePresetName = m_presetRenameBuf;
                m_renamePresetId = -1;
                memset(m_presetRenameBuf, 0, sizeof(m_presetRenameBuf));
            }
        }
        myui::EndAnimatedHeader();
    }

    if (myui::BeginAnimatedHeader("Delete Preset", true))
    {
        ImGui::SetNextItemWidth(200);
        if (myui::StyledBeginCombo("##DeleteCombo", m_deletePresetId > 0
            ? [&]() -> const char* {
                for (auto& p : engine.presetList)
                    if (p.id == m_deletePresetId) return p.name.c_str();
                return "Select...";
            }() : "Select..."))
        {
            for (auto& preset : engine.presetList)
            {
                if (preset.id == engine.activePresetId)
                    continue;
                if (myui::PillSelectable(preset.name.c_str(), preset.id == m_deletePresetId, 200.0f))
                {
                    m_deletePresetId = preset.id;
                    m_confirmDelete = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            myui::StyledEndCombo();
        }

        if (m_deletePresetId > 0)
        {
            if (!m_confirmDelete)
            {
                if (ImGui::Button("Delete"))
                    m_confirmDelete = true;
            }
            else
            {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Are you sure?");
                ImGui::SameLine();
                if (ImGui::Button("Yes, Delete"))
                {
                    engine.database->DeletePreset(m_deletePresetId);
                    engine.database->GetPresetList(engine.serverName, engine.presetList);
                    m_deletePresetId = -1;
                    m_confirmDelete = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    m_confirmDelete = false;
            }
        }
        myui::EndAnimatedHeader();
    }

    ImGui::End();
}
