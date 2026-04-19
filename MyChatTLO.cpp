#include "MyChatTLO.h"
#include "MQMyChat.h"

MyChatChannelType* pMyChatChannelType = nullptr;
MyChatType* pMyChatType = nullptr;

MyChatChannelType::MyChatChannelType() : MQ2Type("MyChatChannel")
{
	ScopedTypeMember(Members, Name);
	ScopedTypeMember(Members, ID);
	ScopedTypeMember(Members, Enabled);
	ScopedTypeMember(Members, TabOrder);
	ScopedTypeMember(Members, PopOut);
	ScopedTypeMember(Members, Echo);
	ScopedTypeMember(Members, MainEnable);
}

bool MyChatChannelType::GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest)
{
	MQTypeMember* pMember = FindMember(Member);
	if (!pMember)
		return false;

	if (!g_chatEngine)
		return false;

	int channelId = VarPtr.Int;
	auto it = g_chatEngine->settings.channels.find(channelId);
	if (it == g_chatEngine->settings.channels.end())
		return false;

	const ChatChannel& channel = it->second;

	switch (static_cast<Members>(pMember->ID))
	{
	case Members::Name:
		strcpy_s(DataTypeTemp, MAX_STRING, channel.name.c_str());
		Dest.Ptr = &DataTypeTemp[0];
		Dest.Type = mq::datatypes::pStringType;
		return true;

	case Members::ID:
		Dest.Int = channel.channelId;
		Dest.Type = mq::datatypes::pIntType;
		return true;

	case Members::Enabled:
		Dest.Set(channel.enabled);
		Dest.Type = mq::datatypes::pBoolType;
		return true;

	case Members::TabOrder:
		Dest.Int = channel.tabOrder;
		Dest.Type = mq::datatypes::pIntType;
		return true;

	case Members::PopOut:
		Dest.Set(channel.popOut);
		Dest.Type = mq::datatypes::pBoolType;
		return true;

	case Members::Echo:
		strcpy_s(DataTypeTemp, MAX_STRING, channel.echo.c_str());
		Dest.Ptr = &DataTypeTemp[0];
		Dest.Type = mq::datatypes::pStringType;
		return true;

	case Members::MainEnable:
		Dest.Set(channel.mainEnable);
		Dest.Type = mq::datatypes::pBoolType;
		return true;
	}

	return false;
}

bool MyChatChannelType::ToString(MQVarPtr VarPtr, char* Destination)
{
	if (!g_chatEngine)
		return false;

	int channelId = VarPtr.Int;
	auto it = g_chatEngine->settings.channels.find(channelId);
	if (it == g_chatEngine->settings.channels.end())
		return false;

	strcpy_s(Destination, MAX_STRING, it->second.name.c_str());
	return true;
}

MyChatType::MyChatType() : MQ2Type("MyChat")
{
	ScopedTypeMember(Members, Version);
	ScopedTypeMember(Members, Channel);
	ScopedTypeMember(Members, ChannelCount);
	ScopedTypeMethod(Methods, Send);
}

bool MyChatType::GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest)
{
	MQTypeMember* pMethod = FindMethod(Member);
	if (pMethod)
	{
		switch (static_cast<Methods>(pMethod->ID))
		{
		case Methods::Send:
			if (g_chatEngine && Index[0])
			{
				std::string input(Index);
				size_t sep = input.find(',');
				if (sep != std::string::npos)
				{
					std::string channel = input.substr(0, sep);
					std::string message = input.substr(sep + 1);
					while (!channel.empty() && channel.front() == ' ') channel.erase(channel.begin());
					while (!channel.empty() && channel.back() == ' ') channel.pop_back();
					while (!message.empty() && message.front() == ' ') message.erase(message.begin());
					g_chatEngine->SendToChannel(channel, message);
					Dest.Set(true);
					Dest.Type = mq::datatypes::pBoolType;
					return true;
				}
			}
			Dest.Set(false);
			Dest.Type = mq::datatypes::pBoolType;
			return true;
		}
	}

	MQTypeMember* pMember = FindMember(Member);
	if (!pMember)
		return false;

	switch (static_cast<Members>(pMember->ID))
	{
	case Members::Version:
		Dest.Float = 0.1f;
		Dest.Type = mq::datatypes::pFloatType;
		return true;

	case Members::Channel:
		if (!g_chatEngine)
			return false;

		if (Index[0])
		{
			char* end = nullptr;
			long numId = strtol(Index, &end, 10);
			if (end != Index && *end == '\0')
			{
				auto it = g_chatEngine->settings.channels.find(static_cast<int>(numId));
				if (it != g_chatEngine->settings.channels.end())
				{
					Dest.Int = it->second.channelId;
					Dest.Type = pMyChatChannelType;
					return true;
				}
			}
			else
			{
				for (const auto& [id, ch] : g_chatEngine->settings.channels)
				{
					if (ci_equals(ch.name, Index))
					{
						Dest.Int = id;
						Dest.Type = pMyChatChannelType;
						return true;
					}
				}
			}
		}
		return false;

	case Members::ChannelCount:
		if (!g_chatEngine)
			return false;

		Dest.Int = static_cast<int>(g_chatEngine->settings.channels.size());
		Dest.Type = mq::datatypes::pIntType;
		return true;
	}

	return false;
}

bool MyChatType::ToString(MQVarPtr VarPtr, char* Destination)
{
	strcpy_s(Destination, MAX_STRING, "MQMyChat");
	return true;
}

bool MyChatType::dataMyChat(const char* szIndex, MQTypeVar& Ret)
{
	Ret.DWord = 1;
	Ret.Type = pMyChatType;
	return true;
}

void RegisterMyChatTLO()
{
	pMyChatChannelType = new MyChatChannelType();
	pMyChatType = new MyChatType();
	AddTopLevelObject("MyChat", MyChatType::dataMyChat);
}

void UnregisterMyChatTLO()
{
	RemoveTopLevelObject("MyChat");

	delete pMyChatType;
	pMyChatType = nullptr;

	delete pMyChatChannelType;
	pMyChatChannelType = nullptr;
}
