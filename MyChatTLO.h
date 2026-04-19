#pragma once

#include <mq/Plugin.h>

class MyChatChannelType : public MQ2Type
{
public:
	enum class Members
	{
		Name,
		ID,
		Enabled,
		TabOrder,
		PopOut,
		Echo,
		MainEnable,
	};

	MyChatChannelType();

	bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override;
	bool ToString(MQVarPtr VarPtr, char* Destination) override;
};

class MyChatType : public MQ2Type
{
public:
	enum class Members
	{
		Version,
		Channel,
		ChannelCount,
	};

	enum class Methods
	{
		Send,
	};

	MyChatType();

	bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override;
	bool ToString(MQVarPtr VarPtr, char* Destination) override;

	static bool dataMyChat(const char* szIndex, MQTypeVar& Ret);
};

extern MyChatChannelType* pMyChatChannelType;
extern MyChatType* pMyChatType;

void RegisterMyChatTLO();
void UnregisterMyChatTLO();
