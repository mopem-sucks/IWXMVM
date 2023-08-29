#pragma once
#include "GameInterface.hpp"
#include "Components/CameraManager.hpp"

namespace IWXMVM
{
	class Mod 
	{
	public:
		static void Initialize(GameInterface* gameInterface);

		static GameInterface* GetGameInterface() { return internalGameInterface; }
	private:
		static GameInterface* internalGameInterface;
	};
}