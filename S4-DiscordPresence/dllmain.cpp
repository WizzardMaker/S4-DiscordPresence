// dllmain.cpp : Definiert den Einstiegspunkt für die DLL-Anwendung.
#include "pch.h"
#pragma comment(lib, "S4ModApi")
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <string>
#include <chrono>

struct DiscordState {
    discord::User currentUser;

    std::unique_ptr<discord::Core> core;
};

enum DiscordRPState {
    MainMenu,
    LobbyCreation,
    InGame,
    EndGame,
    UNKNOWN,
    NO_DISCORD
} oldState;


static S4API s4; // the interface to the Settlers 4 Mod API

HRESULT S4HCALL S4FrameProc(LPDIRECTDRAWSURFACE7 lpSurface, INT32 iPillarboxWidth, LPVOID lpReserved);
void SetDiscordRPState(DiscordRPState newState,const char* message, const char* subMessage);

std::wstring ExePath() {
    TCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    return std::wstring(buffer).substr(0, pos);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    static DLL_DIRECTORY_COOKIE s = NULL;
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        s4 = S4ApiCreate(); // get an interface to the mod api
        if (NULL == s4) break;

        s4->AddFrameListener(S4FrameProc);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}



DiscordState state{};
bool started = false;
void Start() {
    started = true;
    DWORD a = 0;
    static HMODULE testModule = NULL;
    auto s = (ExePath() + L"\\plugins\\discord\\discord_game_sdk.dll").c_str();
    testModule = LoadLibrary(s);

    a = GetLastError();


    discord::Core* core{};

    auto result = discord::Core::Create(770367463174438922, DiscordCreateFlags_NoRequireDiscord, &core);
    
    if (result != discord::Result::Ok) {
        std::cout << "Failed to instantiate discord core! (err " << static_cast<int>(result)
            << ")\n";

        OutputDebugStringA("Failed to load Discord");
        oldState = NO_DISCORD;
        return;
        //std::exit(-1);
    }

    state.core.reset(core);
    oldState = UNKNOWN;
}

void SetDiscordRPState(DiscordRPState newState, const char * message, const char *subMessage = "") {
    if(oldState == NO_DISCORD || oldState == newState)
        return;

    oldState = newState;

    discord::Activity activity{};
    if(subMessage != "")
        activity.SetDetails(subMessage);

    activity.SetState(message);


    if (newState == InGame) {
        using namespace std::chrono;
        activity.GetTimestamps().SetStart(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    }
    //activity.GetAssets().SetSmallImage("the");
    //activity.GetAssets().SetSmallText("i mage");
    activity.GetAssets().SetLargeImage("logo_en");
    activity.GetAssets().SetLargeText("Community Patch!");
    activity.SetType(discord::ActivityType::Playing);
    state.core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
        std::cout << ((result == discord::Result::Ok) ? "Succeeded" : "Failed")
            << " updating activity!\n";
    });
}

void UpdateState() {
    if (s4->IsCurrentlyOnScreen(S4_SCREEN_MULTIPLAYER)) {
        SetDiscordRPState(DiscordRPState::LobbyCreation, "Creating a multiplayer lobby");
        return;
    }
    if (s4->IsCurrentlyOnScreen(S4_SCREEN_MAINMENU)) {
        SetDiscordRPState(DiscordRPState::MainMenu, "Chilling in the main menu..");
        return;
    }

    if (s4->IsCurrentlyOnScreen(S4_SCREEN_INGAME)) {
        if(oldState == InGame)
            return;

        int players = s4->GetNumberOfPlayers();
        S4_OBJECT_TYPE currentTribe = s4->GetPlayerTribe();
        std::string tribe = (currentTribe == S4_OBJECT_TRIBE_VIKING ? "Vikings" :
            currentTribe == S4_OBJECT_TRIBE_TROJAN ? "Trojans" :
            currentTribe == S4_OBJECT_TRIBE_ROMAN ? "Romans" :
            currentTribe == S4_OBJECT_TRIBE_MAYA ? "Mayans" :
            currentTribe == S4_OBJECT_TRIBE_DARK ? "The dark tribe" : "");
        std::string s = tribe +" vs. " + std::to_string(players-1) + " others!";
        SetDiscordRPState(DiscordRPState::InGame, s.c_str(), "Ingame");
        return;
    }

}

//To get "tick" events even in the main menu
HRESULT S4HCALL S4FrameProc(LPDIRECTDRAWSURFACE7 lpSurface, INT32 iPillarboxWidth, LPVOID lpReserved) {
    try{
        if(started == false){
            Start();

            SetDiscordRPState(DiscordRPState::MainMenu, "Chilling in the main menu..");
        }

        if (oldState == NO_DISCORD)
            return 0;

        UpdateState();

        state.core->RunCallbacks();
    } catch (std::exception e) {
        OutputDebugStringA(e.what());
    }
    return 0;
}