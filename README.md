# MQMyChat

Event-driven, filtered, tabbed chat window plugin for MacroQuest. Routes incoming EQ chat through configurable channels using pattern matching, applies color-coded filters with token substitution, and renders each channel in its own ImGui tab with Zep console widgets. Supports Lua-style pattern matching, SQLite-backed presets, popout windows, and a TLO API for external script integration.

## Getting Started

```txt
/plugin MQMyChat
```

On first load, default channels are seeded (Combat, Crits, Tells, Say, Shout, OOC, Loot, Faction, Exp, Consider, NPC, and more). Settings persist in `resources/MyChat.db` using SQLite with WAL mode for multi-character safety.

Users migrating from the Lua MyChat module can copy their existing database to the resources folder for instant migration. The schema is cross-compatible.

## Commands

```txt
/mychat              Toggle main window
/mychat show         Show main window
/mychat hide         Hide main window
/mychat send <ch> <msg>  Send message to channel (creates if missing)
/mychat clear        Clear main console
/mychat clear <ch>   Clear specific channel
/mychat config       Toggle config window
/mychat reload       Reload settings from database
/mychat lock         Lock window position
/mychat unlock       Unlock window position
/mychat preset <name>  Switch to named preset
```

## TLO

```txt
${MyChat}                       Plugin name string
${MyChat.Version}               Plugin version (float)
${MyChat.ChannelCount}          Number of channels
${MyChat.Channel[name]}         Channel by name
${MyChat.Channel[id]}           Channel by numeric ID
${MyChat.Channel[name].Name}    Channel name
${MyChat.Channel[name].ID}      Channel ID
${MyChat.Channel[name].Enabled} Channel enabled state
${MyChat.Send[channel,message]} Send message to channel (creates if missing)
```

## Features

- Blech pattern matching, same `#*#` / `#1#` engine behind `mq.event()`
- Dual pattern support with Lua patterns (`%d+`, `^You`) and plain substring matching
- Token substitution: M3, N3, P3, PT1, PT3, M1, TK1, RL, G1-G5, H1, GP1, NO2
- Color filters with per-filter RGBA colors and event default color fallback
- Reorderable tabs, drag to reorder, persisted across sessions
- Popout windows, any channel can be popped into its own window
- Presets to save, load, copy, rename, and delete channel configurations
- Configurable focus key to jump the cursor into the chat input field
- Per-channel font size with Ctrl+/-/= support, persisted between sessions
- 11 built-in ImGui themes
- Lua integration so MyUI modules auto-detect the plugin via TLO and route output to it
- Zep console with full EQ link support (item, player, spell links)

## Configuration

Settings are stored in SQLite (`resources/MyChat.db`) with these tables:

- `presets` - Named preset configurations per server
- `channels` - Channel definitions (name, echo, tabOrder, fontSize, etc.)
- `events` - Blech pattern strings per channel
- `filters` - Match patterns with RGBA colors per event
- `global_settings` - Per-character settings (theme, timestamps, focus key, etc.)
- `char_active_preset` - Active preset per character
- `char_channel_overrides` - Per-character channel mix-and-match

## Authors

* Grimmier
