#include <mq/Plugin.h>
#include "MQMyChat.h"
#include "MyChatDatabase.h"
#include "MyChatRenderer.h"
#include "MyChatTLO.h"

#include <zep/display.h>
#include <filesystem>
#include <fmt/format.h>

extern "C" {
#include <luajit/lua.h>
#include <luajit/lauxlib.h>
#include <luajit/lualib.h>
}

PreSetup("MQMyChat");
PLUGIN_VERSION(0.1);

MyChatEngine* g_chatEngine = nullptr;
static MyChatRenderer s_renderer;

static void MyChatCommand(PlayerClient*, const char* szLine);

PLUGIN_API void InitializePlugin()
{
	g_chatEngine = new MyChatEngine();
	g_chatEngine->Initialize();
	AddCommand("/mychat", MyChatCommand);
	RegisterMyChatTLO();
}

PLUGIN_API void ShutdownPlugin()
{
	UnregisterMyChatTLO();
	RemoveCommand("/mychat");
	g_chatEngine->Shutdown();
	delete g_chatEngine;
	g_chatEngine = nullptr;
}

PLUGIN_API void SetGameState(int GameState)
{
	if (!g_chatEngine)
		return;

	if (GameState == GAMESTATE_INGAME)
		g_chatEngine->LoadCharacterSettings();
	else if (GameState == GAMESTATE_CHARSELECT)
		g_chatEngine->UnloadCharacterSettings();
}

PLUGIN_API bool OnIncomingChat(const char* Line, DWORD Color)
{
	if (g_chatEngine)
		g_chatEngine->ProcessIncomingChat(Line, static_cast<int>(Color));
	return false;
}

PLUGIN_API void OnWriteChatColor(const char* Line, int Color, int Filter)
{
	if (g_chatEngine)
		g_chatEngine->ProcessWriteChat(Line, Color);
}

PLUGIN_API void OnUpdateImGui()
{
	if (!g_chatEngine || GetGameState() != GAMESTATE_INGAME)
		return;

	s_renderer.RenderMainWindow(*g_chatEngine);
	s_renderer.RenderPopOutWindows(*g_chatEngine);

	if (g_chatEngine->showConfigGUI)
		s_renderer.RenderConfigGUI(*g_chatEngine);

	if (g_chatEngine->showEditGUI)
		s_renderer.RenderEditChannelGUI(*g_chatEngine);
}

PLUGIN_API void OnPulse()
{
	if (!g_chatEngine)
		return;

	static std::chrono::steady_clock::time_point pulseTimer = std::chrono::steady_clock::now();
	if (std::chrono::steady_clock::now() > pulseTimer)
	{
		pulseTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		g_chatEngine->CleanExpiredClaims();

		if (!g_chatEngine->charName.empty() &&
			g_chatEngine->m_lastSavedShowMain != g_chatEngine->showMainWindow)
		{
			g_chatEngine->SaveWindowVisibility();
		}
	}
}

static void MyChatCommand(PlayerClient*, const char* szLine)
{
	char arg[MAX_STRING] = {};
	GetArg(arg, szLine, 1);

	if (arg[0] == '\0' || ci_equals(arg, "show"))
	{
		g_chatEngine->showMainWindow = true;
		WriteChatf("\at[MQMyChat]\ax Window shown.");
		return;
	}

	if (ci_equals(arg, "hide"))
	{
		g_chatEngine->showMainWindow = false;
		WriteChatf("\at[MQMyChat]\ax Window hidden.");
		return;
	}

	if (ci_equals(arg, "send"))
	{
		char channelName[MAX_STRING] = {};
		GetArg(channelName, szLine, 2);
		if (channelName[0] == '\0')
		{
			WriteChatf("\at[MQMyChat]\ax Usage: /mychat send <channel> <message>");
			return;
		}

		const char* message = GetNextArg(szLine, 2);
		if (!message || message[0] == '\0')
		{
			WriteChatf("\at[MQMyChat]\ax Usage: /mychat send <channel> <message>");
			return;
		}

		g_chatEngine->SendToChannel(channelName, message);
		return;
	}

	if (ci_equals(arg, "clear"))
	{
		char channelName[MAX_STRING] = {};
		GetArg(channelName, szLine, 2);

		if (channelName[0] == '\0')
		{
			if (g_chatEngine->mainConsole)
				g_chatEngine->mainConsole->Clear();
			WriteChatf("\at[MQMyChat]\ax Main console cleared.");
		}
		else
		{
			for (auto& [id, ch] : g_chatEngine->settings.channels)
			{
				if (ci_equals(ch.name, channelName))
				{
					if (ch.console)
						ch.console->Clear();
					WriteChatf("\at[MQMyChat]\ax Channel '%s' cleared.", channelName);
					return;
				}
			}
			WriteChatf("\at[MQMyChat]\ax Channel '%s' not found.", channelName);
		}
		return;
	}

	if (ci_equals(arg, "config"))
	{
		g_chatEngine->showConfigGUI = !g_chatEngine->showConfigGUI;
		return;
	}

	if (ci_equals(arg, "reload"))
	{
		g_chatEngine->UnloadCharacterSettings();
		g_chatEngine->LoadCharacterSettings();
		WriteChatf("\at[MQMyChat]\ax Settings reloaded.");
		return;
	}

	if (ci_equals(arg, "lock"))
	{
		g_chatEngine->settings.windowLocked = true;
		WriteChatf("\at[MQMyChat]\ax Window locked.");
		return;
	}

	if (ci_equals(arg, "unlock"))
	{
		g_chatEngine->settings.windowLocked = false;
		WriteChatf("\at[MQMyChat]\ax Window unlocked.");
		return;
	}

	if (ci_equals(arg, "preset"))
	{
		char presetName[MAX_STRING] = {};
		GetArg(presetName, szLine, 2);
		if (presetName[0] == '\0')
		{
			WriteChatf("\at[MQMyChat]\ax Usage: /mychat preset <name>");
			return;
		}

		for (const auto& preset : g_chatEngine->presetList)
		{
			if (ci_equals(preset.name, presetName))
			{
				g_chatEngine->SaveCharacterSettings();
				g_chatEngine->database->SetActivePreset(g_chatEngine->serverName, g_chatEngine->charName, preset.id);
				g_chatEngine->UnloadCharacterSettings();
				g_chatEngine->LoadCharacterSettings();
				WriteChatf("\at[MQMyChat]\ax Switched to preset '%s'.", presetName);
				return;
			}
		}

		WriteChatf("\at[MQMyChat]\ax Preset '%s' not found.", presetName);
		return;
	}

	WriteChatf("\at[MQMyChat]\ax Unknown command: %s", arg);
	WriteChatf("\at[MQMyChat]\ax Usage: /mychat [show|hide|send|clear|config|reload|lock|unlock|preset]");
}

void MyChatEngine::Initialize()
{
	database = std::make_unique<MyChatDatabase>();
	dbPath = fmt::format("{}\\MyChat.db", gPathResources);
	std::filesystem::path dbDir = std::filesystem::path(dbPath).parent_path();
	std::filesystem::create_directories(dbDir);

	if (!std::filesystem::exists(dbPath))
	{
		std::string luaDbPath = fmt::format("{}\\MyUI\\MyChat\\MyChat.db", gPathConfig);
		if (std::filesystem::exists(luaDbPath))
		{
			std::filesystem::copy_file(luaDbPath, dbPath);
			WriteChatf("\at[MQMyChat]\ax Migrated database from Lua MyChat: %s", luaDbPath.c_str());
		}
	}

	database->Open(dbPath);
	database->InitSchema();
	mainConsole = mq::imgui::ConsoleWidget::Create("MQMyChat##MainConsole");
	m_blech = std::make_unique<Blech>('#', '|', BlechVarCallback);
	m_luaState = luaL_newstate();
	if (m_luaState)
		luaL_openlibs(m_luaState);
}

void MyChatEngine::Shutdown()
{
	UnregisterBlechEvents();

	if (!charName.empty())
		SaveCharacterSettings();

	mainConsole.reset();

	for (auto& [id, ch] : settings.channels)
		ch.console.reset();

	if (m_luaState)
	{
		lua_close(m_luaState);
		m_luaState = nullptr;
	}

	database->Close();
}

void MyChatEngine::LoadCharacterSettings()
{
	if (!pLocalPlayer)
		return;

	charName = pLocalPlayer->Name;
	serverName = GetServerShortName();

	activePresetId = database->GetOrCreatePreset(serverName, charName);
	database->LoadSettings(activePresetId, settings);
	database->LoadGlobalSettings(serverName, charName, settings);
	LoadWindowVisibility();

	if (settings.channels.empty())
		PopulateDefaultChannels();

	for (auto& [id, ch] : settings.channels)
	{
		if (!ch.console)
			ch.console = mq::imgui::ConsoleWidget::Create(fmt::format("MQMyChat##Channel_{}", id).c_str());
	}

	RegisterBlechEvents();
	SortChannels();
	ApplyFontSizes();
	SaveCharacterSettings();
	database->GetPresetList(serverName, presetList);

	activePresetName.clear();
	for (const auto& p : presetList)
	{
		if (p.id == activePresetId)
		{
			activePresetName = p.name;
			break;
		}
	}
}

void MyChatEngine::SaveCharacterSettings()
{
	if (activePresetId < 0)
		return;

	SyncFontSizes();
	database->SaveSettings(activePresetId, settings);
	database->SaveGlobalSettings(serverName, charName, settings);
	SaveWindowVisibility();
}

void MyChatEngine::UnloadCharacterSettings()
{
	if (charName.empty())
		return;

	SaveCharacterSettings();
	UnregisterBlechEvents();

	for (auto& [id, ch] : settings.channels)
		ch.console.reset();

	settings.channels.clear();
	charName.clear();
}

static std::string GetWindowVisibilityIniPath(const std::string& server, const std::string& charName)
{
	if (server.empty() || charName.empty())
		return {};

	std::string path = fmt::format("{}\\MQMyChat\\{}\\{}.ini", gPathConfig, server, charName);
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	return path;
}

void MyChatEngine::LoadWindowVisibility()
{
	if (!pLocalPC)
		return;

	std::string path = GetWindowVisibilityIniPath(serverName, charName);
	if (path.empty())
		return;

	showMainWindow = GetPrivateProfileBool("Windows", "ShowMain", true, path);
	m_lastSavedShowMain = showMainWindow;
}

void MyChatEngine::SaveWindowVisibility()
{
	std::string path = GetWindowVisibilityIniPath(serverName, charName);
	if (path.empty())
		return;

	WritePrivateProfileBool("Windows", "ShowMain", showMainWindow, path);
	m_lastSavedShowMain = showMainWindow;
}

void MyChatEngine::RegisterBlechEvents()
{
	if (!m_blech)
		return;

	m_blech->Reset();
	m_blechEventMap.clear();

	for (auto& [channelId, ch] : settings.channels)
	{
		if (!ch.enabled)
			continue;

		for (auto& evt : ch.events)
		{
			if (!evt.enabled || evt.eventString.empty())
				continue;

			auto* data = new BlechEventData{ channelId, evt.eventIndex };
			unsigned int id = m_blech->AddEvent(evt.eventString.c_str(), BlechCallback, data);
			evt.blechId = id;
			m_blechEventMap[id] = *data;
			delete data;
		}
	}
}

void MyChatEngine::UnregisterBlechEvents()
{
	if (!m_blech)
		return;

	m_blech->Reset();
	m_blechEventMap.clear();
}

void CALLBACK MyChatEngine::BlechCallback(unsigned int id, void* pData, PBLECHVALUE pValues)
{
	if (!g_chatEngine)
		return;

	auto it = g_chatEngine->m_blechEventMap.find(id);
	if (it == g_chatEngine->m_blechEventMap.end())
		return;

	const BlechEventData& evtData = it->second;

	if (g_chatEngine->m_currentLine)
		g_chatEngine->RouteToChannel(evtData.channelId, evtData.eventIndex, g_chatEngine->m_currentLine, g_chatEngine->m_currentColor);
}

unsigned int CALLBACK MyChatEngine::BlechVarCallback(char* varName, char* value, size_t valueLen)
{
	return 0;
}

void MyChatEngine::ProcessIncomingChat(const char* line, int color)
{
	if (charName.empty() || !m_blech)
		return;

	m_currentLine = line;
	m_currentColor = color;
	m_blech->Feed(line, strlen(line));
	m_currentLine = nullptr;

	if (enableSpam)
	{
		std::string lineStr(line);
		if (m_claimedLines.find(lineStr) == m_claimedLines.end())
		{
			auto spamIt = settings.channels.find(CHANNEL_SPAM);
			if (spamIt != settings.channels.end() && spamIt->second.console)
			{
				spamIt->second.console->AppendText(line, MQColor(240, 240, 240), true);
				if (spamIt->second.mainEnable && mainConsole)
					mainConsole->AppendText(line, MQColor(240, 240, 240), true);
			}
		}
	}
}

void MyChatEngine::ProcessWriteChat(const char* line, int color)
{
	if (charName.empty() || !m_blech)
		return;

	m_currentLine = line;
	m_currentColor = color;
	m_blech->Feed(line, strlen(line));
	m_currentLine = nullptr;
}

void MyChatEngine::RouteToChannel(int channelId, int eventIndex, const char* line, int color)
{
	auto chIt = settings.channels.find(channelId);
	if (chIt == settings.channels.end())
		return;

	ChatChannel& channel = chIt->second;

	bool filtered = false;
	MQColor outputColor(240, 240, 240, 255);
	bool hasNonDefaultFilters = false;
	bool matched = false;

	for (const auto& evt : channel.events)
	{
		if (evt.eventIndex != eventIndex)
			continue;

		for (const auto& filter : evt.filters)
		{
			if (!filter.enabled)
				continue;

			if (filter.filterIndex == 0)
			{
				outputColor = filter.color;
				continue;
			}

			hasNonDefaultFilters = true;

			if (MatchFilter(line, filter))
			{
				if (filter.hidden)
				{
					filtered = true;
					break;
				}

				outputColor = filter.color;
				matched = true;
				break;
			}
		}
		break;
	}

	if (filtered)
		return;

	if (hasNonDefaultFilters && !matched)
		return;

	if (ci_equals(channel.name, "consider") && pTarget)
	{
		int conVal = ConColor(pTarget);
		switch (conVal)
		{
		case CONCOLOR_GREY:      outputColor = MQColor(153, 153, 153); break;
		case CONCOLOR_GREEN:     outputColor = MQColor(0, 255, 0); break;
		case CONCOLOR_LIGHTBLUE: outputColor = MQColor(94, 180, 255); break;
		case CONCOLOR_BLUE:      outputColor = MQColor(0, 0, 255); break;
		case CONCOLOR_WHITE:     outputColor = MQColor(255, 255, 255); break;
		case CONCOLOR_YELLOW:    outputColor = MQColor(255, 255, 0); break;
		case CONCOLOR_RED:       outputColor = MQColor(230, 26, 26); break;
		default: break;
		}
	}

	if (channel.console)
		channel.console->AppendText(line, outputColor, true);

	if (channel.mainEnable && mainConsole)
		mainConsole->AppendText(line, outputColor, true);

	std::string lineStr(line);
	m_claimedLines[lineStr] = ClaimedLine{
		std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(CLAIMED_TTL * 1000))
	};
}

void MyChatEngine::SortChannels()
{
	sortedChannels.clear();
	for (const auto& [id, ch] : settings.channels)
	{
		sortedChannels.push_back({ id, ch.name, ch.tabOrder });
	}

	std::sort(sortedChannels.begin(), sortedChannels.end(),
		[](const SortedChannel& a, const SortedChannel& b)
		{
			if (a.tabOrder != b.tabOrder)
				return a.tabOrder < b.tabOrder;
			return a.name < b.name;
		});
}

void MyChatEngine::PopulateDefaultChannels()
{
	if (database && activePresetId >= 0)
	{
		database->SeedDefaultChannels(activePresetId);
		database->LoadSettings(activePresetId, settings);
	}
}

static void SetConsoleFontSize(const std::shared_ptr<mq::imgui::ConsoleWidget>& console, int fontSize)
{
	if (!console || fontSize <= 0)
		return;

	auto& display = console->GetEditor().GetDisplay();
	auto& font = display.GetFont(Zep::ZepTextType::Text);
	if (font.GetPixelHeight() != fontSize)
		font.SetPixelHeight(fontSize);
}

void MyChatEngine::ApplyFontSizes()
{
	SetConsoleFontSize(mainConsole, settings.mainFontSize);

	for (auto& [id, ch] : settings.channels)
	{
		if (ch.console && ch.fontSize > 0)
			SetConsoleFontSize(ch.console, ch.fontSize);
	}
}

static int GetConsoleFontSize(const std::shared_ptr<mq::imgui::ConsoleWidget>& console)
{
	if (!console)
		return 16;
	return console->GetEditor().GetDisplay().GetFont(Zep::ZepTextType::Text).GetPixelHeight();
}

void MyChatEngine::SyncFontSizes()
{
	settings.mainFontSize = GetConsoleFontSize(mainConsole);

	for (auto& [id, ch] : settings.channels)
	{
		if (ch.console)
			ch.fontSize = GetConsoleFontSize(ch.console);
	}
}

void MyChatEngine::SendToChannel(const std::string& channelName, const std::string& message)
{
	for (auto& [id, ch] : settings.channels)
	{
		if (ci_equals(ch.name, channelName))
		{
			if (ch.console)
				ch.console->AppendText(message, MQColor(240, 240, 240), true);

			if (ch.mainEnable && mainConsole)
				mainConsole->AppendText(message, MQColor(240, 240, 240), true);
			return;
		}
	}

	int newId = GetNextChannelId();
	CreateChannel(channelName, newId);

	auto it = settings.channels.find(newId);
	if (it != settings.channels.end())
	{
		if (it->second.console)
			it->second.console->AppendText(message, MQColor(240, 240, 240), true);

		if (it->second.mainEnable && mainConsole)
			mainConsole->AppendText(message, MQColor(240, 240, 240), true);
	}
}

void MyChatEngine::CreateChannel(const std::string& name, int channelId)
{
	if (channelId < 0)
		channelId = GetNextChannelId();

	ChatChannel ch;
	ch.channelId = channelId;
	ch.name = name;
	ch.console = mq::imgui::ConsoleWidget::Create(fmt::format("MQMyChat##Channel_{}", channelId).c_str());
	settings.channels[channelId] = std::move(ch);
	SortChannels();
}

int MyChatEngine::GetNextChannelId() const
{
	int maxId = 0;
	for (const auto& [id, ch] : settings.channels)
	{
		if (id < CHANNEL_RESERVED_START && id > maxId)
			maxId = id;
	}
	return maxId + 1;
}

void MyChatEngine::CleanExpiredClaims()
{
	auto now = std::chrono::steady_clock::now();
	for (auto it = m_claimedLines.begin(); it != m_claimedLines.end();)
	{
		if (now > it->second.expireTime)
			it = m_claimedLines.erase(it);
		else
			++it;
	}
}

static bool IsLuaPattern(const std::string& pattern)
{
	for (size_t i = 0; i + 1 < pattern.size(); ++i)
	{
		if (pattern[i] == '%')
			return true;
	}
	return false;
}

bool MyChatEngine::PatternFind(const std::string& text, const std::string& pattern) const
{
	if (pattern.empty())
		return true;

	if (IsLuaPattern(pattern) && m_luaState)
	{
		lua_getglobal(m_luaState, "string");
		lua_getfield(m_luaState, -1, "find");
		lua_pushstring(m_luaState, text.c_str());
		lua_pushstring(m_luaState, pattern.c_str());
		int status = lua_pcall(m_luaState, 2, 1, 0);
		bool found = (status == 0 && !lua_isnil(m_luaState, -1));
		lua_settop(m_luaState, 0);
		return found;
	}

	if (!pattern.empty() && pattern[0] == '^')
	{
		std::string anchor = pattern.substr(1);
		if (anchor.empty())
			return true;
		return text.size() >= anchor.size() && text.compare(0, anchor.size(), anchor) == 0;
	}

	return text.find(pattern) != std::string::npos;
}

static bool IsNPCName(const std::string& name)
{
	if (GetGameState() != GAMESTATE_INGAME || !pLocalPlayer)
		return false;

	std::string searchName = name;
	for (char& c : searchName)
		if (c == ' ') c = '_';

	if (SPAWNINFO* pNPC = GetSpawnByPartialName(searchName.c_str()))
	{
		if (pNPC->Type == SPAWN_NPC)
			return true;
	}

	return false;
}

static std::pair<bool, std::string> ExtractNameFromLine(const std::string& line)
{
	static const char* keywords[] = {
		"pet tells you",
		"tells you,",
		"says to you,",
		" says, '",
		" says '",
		" says,",
		" whispers,",
		" shouts,",
		" slashes",
		" pierces",
		" kicks",
		" crushes",
		" bashes",
		" hits",
		" tries",
		" backstabs",
		" bites",
		" begins",
	};

	for (const char* keyword : keywords)
	{
		size_t pos = line.find(keyword);
		if (pos == std::string::npos || pos == 0)
			continue;

		size_t nameEnd = pos;
		while (nameEnd > 0 && line[nameEnd - 1] == ' ')
			nameEnd--;

		if (nameEnd == 0)
			continue;

		std::string name = line.substr(0, nameEnd);

		bool isNPC = IsNPCName(name);
		return { isNPC, name };
	}

	return { false, "" };
}

static std::string GetPetName()
{
	if (pLocalPlayer && pLocalPlayer->PetID > 0)
	{
		if (PlayerClient* pPet = GetSpawnByID(pLocalPlayer->PetID))
			return pPet->DisplayedName;
	}
	return "NO PET";
}

std::string MyChatEngine::SubstituteTokens(const std::string& pattern, const std::string& line) const
{
	if (!pLocalPlayer)
		return pattern;

	std::string result = pattern;

	auto replace = [&](const std::string& token, const std::string& value) -> bool {
		size_t pos = result.find(token);
		if (pos != std::string::npos)
		{
			result.replace(pos, token.length(), value);
			return true;
		}
		return false;
	};

	if (replace("M3", pLocalPlayer->Name)) return result;

	if (result.find("PT1") != std::string::npos)
	{
		if (replace("PT1", GetPetName())) return result;
	}

	if (result.find("PT3") != std::string::npos)
	{
		auto [isNPC, npcName] = ExtractNameFromLine(line);
		if (!npcName.empty())
		{
			bool tagged = false;
			std::string petSearch = npcName;
			for (char& c : petSearch)
				if (c == ' ') c = '_';
			PlayerClient* pSpawn = GetSpawnByPartialName(petSearch.c_str());
			if (pSpawn && pSpawn->MasterID > 0)
			{
				PlayerClient* pMaster = GetSpawnByID(pSpawn->MasterID);
				if (pMaster && pLocalPC && pLocalPC->pGroupInfo)
				{
					for (int g = 1; g < MAX_GROUP_SIZE; ++g)
					{
						CGroupMember* pMember = pLocalPC->pGroupInfo->GetGroupMember(g);
						if (pMember && ci_equals(pMember->GetName(), pMaster->Name))
						{
							replace("PT3", npcName);
							tagged = true;
							break;
						}
					}
				}
			}
			if (!tagged)
				replace("PT3", GetPetName());
		}
		else
		{
			replace("PT3", GetPetName());
		}
		return result;
	}

	if (result.find("M1") != std::string::npos)
	{
		std::string maName = "NO MA";
		if (pLocalPC && pLocalPC->pGroupInfo)
		{
			for (int g = 1; g < MAX_GROUP_SIZE; ++g)
			{
				if (CGroupMember* pMember = pLocalPC->pGroupInfo->GetGroupMember(g))
				{
					if (pMember->IsMainAssist())
					{
						maName = pMember->GetName();
						break;
					}
				}
			}
		}
		if (replace("M1", maName)) return result;
	}

	if (result.find("TK1") != std::string::npos)
	{
		std::string tankName = "NO TANK";
		if (pLocalPC && pLocalPC->pGroupInfo)
		{
			for (int g = 1; g < MAX_GROUP_SIZE; ++g)
			{
				if (CGroupMember* pMember = pLocalPC->pGroupInfo->GetGroupMember(g))
				{
					if (pMember->IsMainTank())
					{
						tankName = pMember->GetName();
						break;
					}
				}
			}
		}
		if (replace("TK1", tankName)) return result;
	}

	if (result.find("P3") != std::string::npos)
	{
		auto [isNPC, pcName] = ExtractNameFromLine(line);
		if (!isNPC && !pcName.empty() && pcName != GetPetName())
			replace("P3", pcName);
		else
			replace("P3", "None");
		return result;
	}

	if (result.find("N3") != std::string::npos)
	{
		auto [isNPC, npcName] = ExtractNameFromLine(line);
		if (!isNPC && !npcName.empty())
		{
			std::string searchName = npcName;
			for (char& c : searchName)
				if (c == ' ') c = '_';

			if (SPAWNINFO* pNPC = GetSpawnByPartialName(searchName.c_str()))
			{
				if (pNPC->Type == SPAWN_NPC)
				{
					isNPC = true;
					npcName = pNPC->DisplayedName;
				}
			}
		}
		if (isNPC && !npcName.empty())
			replace("N3", npcName);
		else
			replace("N3", "None");
		return result;
	}

	if (result.find("RL") != std::string::npos)
	{
		std::string rlName = "NO RAID";
		if (pRaid && pRaid->RaidLeaderName[0])
			rlName = pRaid->RaidLeaderName;
		if (replace("RL", rlName)) return result;
	}

	for (int i = 1; i <= 5; ++i)
	{
		std::string token = fmt::format("G{}", i);
		if (result.find(token) != std::string::npos)
		{
			std::string memberName = "NO GROUP";
			if (pLocalPC && pLocalPC->pGroupInfo)
			{
				auto* pMember = pLocalPC->pGroupInfo->GetGroupMember(i);
				if (pMember) memberName = pMember->GetName();
			}
			if (replace(token, memberName)) return result;
		}
	}

	if (result.find("H1") != std::string::npos)
	{
		if (pLocalPC && pLocalPC->pGroupInfo)
		{
			for (int g = 1; g < MAX_GROUP_SIZE; ++g)
			{
				CGroupMember* pMember = pLocalPC->pGroupInfo->GetGroupMember(g);
				if (!pMember) continue;
				PlayerClient* pPlayer = pMember->GetPlayer();
				if (!pPlayer) continue;
				int classId = pPlayer->GetClass();
				if (classId == 2 || classId == 6 || classId == 10)
				{
					std::string tryResult = result;
					size_t pos = tryResult.find("H1");
					if (pos != std::string::npos)
						tryResult.replace(pos, 2, pMember->GetName());
					if (PatternFind(line, tryResult))
						return tryResult;
				}
			}
		}
		replace("H1", "NO HEALER");
		return result;
	}

	if (result.find("GP1") != std::string::npos)
	{
		if (pLocalPC && pLocalPC->pGroupInfo)
		{
			for (int g = 1; g < MAX_GROUP_SIZE; ++g)
			{
				CGroupMember* pMember = pLocalPC->pGroupInfo->GetGroupMember(g);
				if (!pMember) continue;
				std::string tryResult = result;
				size_t pos = tryResult.find("GP1");
				if (pos != std::string::npos)
					tryResult.replace(pos, 3, pMember->GetName());
				if (PatternFind(line, tryResult))
					return tryResult;
			}
		}
		replace("GP1", "NO GROUP");
		return result;
	}

	return result;
}

bool MyChatEngine::MatchFilter(const std::string& line, const ChatFilter& filter) const
{
	if (filter.filterString.empty())
		return true;

	std::string matchStr = filter.filterString;
	bool invert = false;

	if (matchStr.size() > 3 && matchStr.substr(0, 3) == "NO2")
	{
		invert = true;
		matchStr = matchStr.substr(3);
	}

	matchStr = SubstituteTokens(matchStr, line);
	bool found = PatternFind(line, matchStr);

	return invert ? !found : found;
}
