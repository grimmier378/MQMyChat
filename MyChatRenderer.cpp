#include "MyChatRenderer.h"
#include "MQMyChat.h"
#include "MyChatDatabase.h"
#include "Theme.h"

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
            engine.showConfigGUI = !engine.showConfigGUI;

        if (ImGui::BeginMenu("Options"))
        {
            ImGui::Checkbox("Timestamps", &engine.settings.timeStamps);
            ImGui::Checkbox("Links", &engine.settings.doLinks);
            ImGui::Checkbox("Local Echo", &engine.settings.localEcho);
            ImGui::Checkbox("Auto Scroll", &engine.settings.autoScroll);
            ImGui::Checkbox("Log Commands", &engine.settings.logCommands);
            if (ImGui::Checkbox("Spam Channel", &engine.enableSpam))
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
            ImGui::Checkbox("Key Focus", &engine.settings.keyFocus);
            if (engine.settings.keyFocus)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                char keyBuf[64] = {};
                strncpy_s(keyBuf, engine.settings.keyName.c_str(), sizeof(keyBuf) - 1);
                if (ImGui::InputText("##KeyName", keyBuf, sizeof(keyBuf)))
                    engine.settings.keyName = keyBuf;
            }
            ImGui::Separator();
            ImGui::Text("Main Echo:");
            ImGui::SetNextItemWidth(150);
            char echoBuf[256] = {};
            strncpy_s(echoBuf, engine.settings.mainEcho.c_str(), sizeof(echoBuf) - 1);
            if (ImGui::InputText("##MainEcho", echoBuf, sizeof(echoBuf)))
                engine.settings.mainEcho = echoBuf;
            ImGui::Separator();
            ImGui::Text("Main Font Size:");
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("##MainFontSize", &engine.settings.mainFontSize))
            {
                if (engine.settings.mainFontSize < 8) engine.settings.mainFontSize = 8;
                if (engine.settings.mainFontSize > 48) engine.settings.mainFontSize = 48;
                engine.ApplyFontSizes();
            }
            ImGui::Separator();
            ImGui::Text("Theme:");
            engine.settings.themeIdx = ImGuiTheme::DrawThemePicker(engine.settings.themeIdx, "MyChatTheme");
            ImGui::Separator();
            if (ImGui::Button("Save Settings"))
                engine.SaveCharacterSettings();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Channels"))
        {
            for (auto& [id, ch] : engine.settings.channels)
            {
                if (id == CHANNEL_SPAM)
                    continue;
                if (ImGui::Checkbox(ch.name.c_str(), &ch.enabled))
                    engine.RefreshBlechEvents();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Links"))
        {
            for (auto& [id, ch] : engine.settings.channels)
            {
                ImGui::Checkbox(fmt::format("{}##links", ch.name).c_str(), &ch.enableLinks);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("PopOut"))
        {
            for (auto& [id, ch] : engine.settings.channels)
            {
                ImGui::Checkbox(fmt::format("{}##popout", ch.name).c_str(), &ch.popOut);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Presets"))
        {
            ImGui::Text("Active: %s", engine.activePresetName.c_str());
            ImGui::Separator();

            for (auto& preset : engine.presetList)
            {
                bool isActive = (preset.id == engine.activePresetId);
                if (ImGui::MenuItem(preset.name.c_str(), nullptr, isActive))
                {
                    if (!isActive)
                    {
                        engine.SaveCharacterSettings();
                        engine.database->SetActivePreset(engine.serverName, engine.charName, preset.id);
                        engine.UnloadCharacterSettings();
                        engine.LoadCharacterSettings();
                    }
                }
            }

            ImGui::Separator();
            ImGui::SetNextItemWidth(150);
            ImGui::InputText("##PresetName", m_tempPresetName, sizeof(m_tempPresetName));
            ImGui::SameLine();
            if (ImGui::Button("Save As New") && m_tempPresetName[0] != '\0')
            {
                engine.database->SaveAsNewPreset(engine.serverName, engine.charName, m_tempPresetName, engine.settings);
                engine.database->GetPresetList(engine.serverName, engine.presetList);
                memset(m_tempPresetName, 0, sizeof(m_tempPresetName));
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
            if (it == engine.settings.channels.end() || !it->second.enabled)
                continue;

            if (it->second.popOut)
                continue;

            std::string tabName = fmt::format("{}##{}", sc.name, sc.id);
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
        if (!ch.popOut || !ch.enabled)
            continue;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
        if (ch.locked)
            windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

        std::string winName = fmt::format("My Chat - {}##MQMyChat_{}{}", ch.name, engine.charName, id);
        bool open = ch.popOut;
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(winName.c_str(), &open, windowFlags))
        {
            if (ImGui::SmallButton(ch.locked ? ICON_MD_LOCK : ICON_MD_LOCK_OPEN))
                ch.locked = !ch.locked;

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_SETTINGS))
            {
                engine.editChannelId = id;
                engine.showEditGUI = true;
            }

            DrawConsole(engine, id);
        }
        ImGui::End();

        if (!open)
            ch.popOut = false;
    }

    ImGuiTheme::ResetTheme(oldStyle);
}

void MyChatRenderer::RenderConfigGUI(MyChatEngine& engine)
{
    if (!engine.showConfigGUI)
        return;

    auto oldStyle = ImGuiTheme::ApplyTheme(engine.settings.themeIdx);

    std::string winName = fmt::format("My Chat - Config##MQMyChat_{}", engine.charName);
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(winName.c_str(), &engine.showConfigGUI, ImGuiWindowFlags_None))
    {
        if (ImGui::BeginTable("##ChannelTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Echo", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Events", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();

            std::vector<int> toRemove;

            for (auto& [id, ch] : engine.settings.channels)
            {
                if (id == CHANNEL_SPAM)
                    continue;

                ImGui::TableNextRow();
                ImGui::PushID(id);

                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##enabled", &ch.enabled))
                    engine.RefreshBlechEvents();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ch.name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ch.echo.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%d", static_cast<int>(ch.events.size()));

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Edit"))
                {
                    engine.editChannelId = id;
                    engine.showEditGUI = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                    toRemove.push_back(id);

                ImGui::PopID();
            }

            ImGui::EndTable();

            for (int rid : toRemove)
            {
                engine.settings.channels.erase(rid);
                engine.SortChannels();
            }
        }

        ImGui::Separator();

        if (m_addingChannel)
        {
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("Name##NewCh", m_tempName, sizeof(m_tempName));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::InputText("Echo##NewCh", m_tempEcho, sizeof(m_tempEcho));
            ImGui::SameLine();
            if (ImGui::Button("Create"))
            {
                if (m_tempName[0] != '\0')
                {
                    int newId = engine.GetNextChannelId();
                    engine.CreateChannel(m_tempName, newId);
                    if (m_tempEcho[0] != '\0')
                        engine.settings.channels[newId].echo = m_tempEcho;
                    engine.SortChannels();
                    memset(m_tempName, 0, sizeof(m_tempName));
                    memset(m_tempEcho, 0, sizeof(m_tempEcho));
                    m_addingChannel = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                memset(m_tempName, 0, sizeof(m_tempName));
                memset(m_tempEcho, 0, sizeof(m_tempEcho));
                m_addingChannel = false;
            }
        }
        else
        {
            if (ImGui::Button("Add Channel"))
                m_addingChannel = true;
        }

        ImGui::Separator();
        if (ImGui::Button("Save All"))
            engine.SaveCharacterSettings();
    }
    ImGui::End();

    ImGuiTheme::ResetTheme(oldStyle);
}

void MyChatRenderer::RenderEditChannelGUI(MyChatEngine& engine)
{
    if (!engine.showEditGUI)
        return;

    auto it = engine.settings.channels.find(engine.editChannelId);
    if (it == engine.settings.channels.end())
    {
        engine.showEditGUI = false;
        return;
    }

    auto& ch = it->second;
    auto oldStyle = ImGuiTheme::ApplyTheme(engine.settings.themeIdx);

    std::string winName = fmt::format("My Chat - Edit: {}##MQMyChat_{}{}", ch.name, engine.charName, engine.editChannelId);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(winName.c_str(), &engine.showEditGUI, ImGuiWindowFlags_None))
    {
        ImGui::Text("Channel: %s (ID: %d)", ch.name.c_str(), ch.channelId);
        ImGui::Separator();

        char nameBuf[256] = {};
        strncpy_s(nameBuf, ch.name.c_str(), sizeof(nameBuf) - 1);
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Name##Edit", nameBuf, sizeof(nameBuf)))
            ch.name = nameBuf;

        char echoBuf[256] = {};
        strncpy_s(echoBuf, ch.echo.c_str(), sizeof(echoBuf) - 1);
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Echo##Edit", echoBuf, sizeof(echoBuf)))
            ch.echo = echoBuf;

        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Font Size##Edit", &ch.fontSize))
        {
            if (ch.fontSize < 8) ch.fontSize = 8;
            if (ch.fontSize > 48) ch.fontSize = 48;
            if (ch.console)
            {
                auto& font = ch.console->GetEditor().GetDisplay().GetFont(Zep::ZepTextType::Text);
                font.SetPixelHeight(ch.fontSize);
            }
        }

        ImGui::Checkbox("Main Enable##Edit", &ch.mainEnable);
        ImGui::SameLine();
        ImGui::Checkbox("Enable Links##Edit", &ch.enableLinks);
        ImGui::SameLine();
        ImGui::Checkbox("PopOut##Edit", &ch.popOut);

        ImGui::Separator();
        ImGui::Text("Events:");

        int eventToRemove = -1;
        for (int ei = 0; ei < static_cast<int>(ch.events.size()); ei++)
        {
            auto& evt = ch.events[ei];
            ImGui::PushID(ei);

            bool headerOpen = ImGui::CollapsingHeader(
                fmt::format("Event {}: {}##evt", ei, evt.eventString.substr(0, 40)).c_str());

            if (headerOpen)
            {
                if (evt.filters.empty())
                {
                    ChatFilter defaultFilter;
                    defaultFilter.filterIndex = 0;
                    defaultFilter.color = MQColor(240, 240, 240);
                    evt.filters.insert(evt.filters.begin(), defaultFilter);
                }

                if (ImGui::Checkbox("Enabled##evt", &evt.enabled))
                    engine.RefreshBlechEvents();
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove Event"))
                    eventToRemove = ei;

                char evtBuf[1024] = {};
                strncpy_s(evtBuf, evt.eventString.c_str(), sizeof(evtBuf) - 1);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("Pattern##evt", evtBuf, sizeof(evtBuf)))
                    evt.eventString = evtBuf;

                auto& defaultFilt = evt.filters[0];
                ImVec4 evtCol = ImVec4(
                    defaultFilt.color.Red / 255.0f,
                    defaultFilt.color.Green / 255.0f,
                    defaultFilt.color.Blue / 255.0f,
                    defaultFilt.color.Alpha / 255.0f);
                ImGui::Text("Event Color:");
                ImGui::SameLine();
                if (ImGui::ColorEdit4("##evtColor", &evtCol.x, ImGuiColorEditFlags_NoInputs))
                {
                    defaultFilt.color.Red = static_cast<uint8_t>(evtCol.x * 255.0f);
                    defaultFilt.color.Green = static_cast<uint8_t>(evtCol.y * 255.0f);
                    defaultFilt.color.Blue = static_cast<uint8_t>(evtCol.z * 255.0f);
                    defaultFilt.color.Alpha = static_cast<uint8_t>(evtCol.w * 255.0f);
                }

                ImGui::Indent();
                if (evt.filters.size() > 1)
                    ImGui::Text("Filters:");

                int filterToRemove = -1;
                int filterSwapFrom = -1, filterSwapTo = -1;
                for (int fi = 1; fi < static_cast<int>(evt.filters.size()); fi++)
                {
                    auto& flt = evt.filters[fi];
                    ImGui::PushID(fi);

                    char fltBuf[1024] = {};
                    strncpy_s(fltBuf, flt.filterString.c_str(), sizeof(fltBuf) - 1);
                    ImGui::SetNextItemWidth(200);
                    if (ImGui::InputText("##fltPattern", fltBuf, sizeof(fltBuf)))
                        flt.filterString = fltBuf;

                    ImGui::SameLine();
                    ImVec4 col = ImVec4(
                        flt.color.Red / 255.0f,
                        flt.color.Green / 255.0f,
                        flt.color.Blue / 255.0f,
                        flt.color.Alpha / 255.0f);
                    if (ImGui::ColorEdit4("##fltColor", &col.x, ImGuiColorEditFlags_NoInputs))
                    {
                        flt.color.Red = static_cast<uint8_t>(col.x * 255.0f);
                        flt.color.Green = static_cast<uint8_t>(col.y * 255.0f);
                        flt.color.Blue = static_cast<uint8_t>(col.z * 255.0f);
                        flt.color.Alpha = static_cast<uint8_t>(col.w * 255.0f);
                    }

                    ImGui::SameLine();
                    ImGui::Checkbox("On##flt", &flt.enabled);

                    ImGui::SameLine();
                    ImGui::Checkbox("Hidden##flt", &flt.hidden);

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
                {
                    std::swap(evt.filters[filterSwapFrom], evt.filters[filterSwapTo]);
                    evt.filters[filterSwapFrom].filterIndex = filterSwapFrom;
                    evt.filters[filterSwapTo].filterIndex = filterSwapTo;
                }

                if (filterToRemove >= 1)
                    evt.filters.erase(evt.filters.begin() + filterToRemove);

                if (ImGui::SmallButton("Add Filter"))
                {
                    ChatFilter newFilter;
                    newFilter.filterIndex = static_cast<int>(evt.filters.size());
                    evt.filters.push_back(newFilter);
                }
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

                ImGui::Unindent();
            }

            ImGui::PopID();
        }

        if (eventToRemove >= 0)
            ch.events.erase(ch.events.begin() + eventToRemove);

        ImGui::Separator();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##NewEvent", m_tempEventString, sizeof(m_tempEventString));
        ImGui::SameLine();
        if (ImGui::Button("Add Event") && m_tempEventString[0] != '\0')
        {
            ChatEvent newEvent;
            newEvent.eventIndex = static_cast<int>(ch.events.size());
            newEvent.eventString = m_tempEventString;
            ch.events.push_back(newEvent);
            memset(m_tempEventString, 0, sizeof(m_tempEventString));
        }

        ImGui::Separator();
        if (ImGui::Button("Save"))
        {
            engine.UnregisterBlechEvents();
            engine.RegisterBlechEvents();
            engine.SortChannels();
            engine.SaveCharacterSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
            engine.showEditGUI = false;
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
        char* s = ch.inputBuffer;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';

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
        char* s = mainInputBuffer;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';

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
