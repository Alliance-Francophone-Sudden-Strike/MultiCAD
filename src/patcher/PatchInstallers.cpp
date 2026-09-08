#include "pch.h"
#include "PatchInstallers.h"
#include "AudioHelper.h"
#include "GameModuleNames.h"
#include "ProfileOverride.h"
#include "UIFilter.h"

bool InstallGamePatches(TargetState& state, uintptr_t base, size_t size, const std::wstring& path)
{
    Zoom::GetState().setMode(Zoom::Mode::Off);
    std::thread([] { AudioHelper::EnsureMaxVolume(); }).detach();

    DllVersionDetector& detector = DllVersionDetector::GetInstance();
    GameVersion version = detector.GetOrDetectGameVersion(DllType::Game, path, base, size);
    DetectionStatus status = detector.GetDetectionStatus(DllType::Game);
    const bool verifiedFusion = status == DetectionStatus::Supported && version == GameVersion::HS_2;

    // "[Game] GameProfile=" forces a profile onto a dll we couldn't identify. Only once the
    // file was read and hashed, otherwise ModuleInfo has nothing to patch against.
    if (status == DetectionStatus::Supported || status == DetectionStatus::UnsupportedHash)
    {
        const GameVersion forced = ProfileOverride::GetProfileOverride(DllType::Game);
        if (forced != GameVersion::UNKNOWN)
        {
            version = forced;
            status = DetectionStatus::Supported;
        }
    }

    ProfileFactory factory;
    auto profile = factory.create(version);

    switch (status)
    {
    case DetectionStatus::NotDetected:
    {
        ShowErrorAsync("MultiCAD couldn't detect game dll hash for some reason.");
        Screen::UpdateToOrigSize();
        return false;
    }
    case DetectionStatus::NotCalculated:
    {
        ShowErrorAsync("MultiCAD couldn't calculate game dll hash for some reason.");
        Screen::UpdateToOrigSize();
        return false;
    }
    case DetectionStatus::Supported:
    {
        // I check profile version separately, because dll can be identified, but there can be no profile for this version
        if (!profile->isUnknown())
            break;
        ShowErrorAsync("MultiCAD identified game dll, but doesn't have patches for it. The game will NOT work correctly. \nTo add support, contact the author of the mod.");
        Screen::UpdateToOrigSize();
        return false;
    }
    case DetectionStatus::UnsupportedHash:
    {
        ShowErrorAsync("MultiCAD couldn't identify game dll and doesn't try to patch it. The game will NOT work correctly."
            "\nTo add support, contact the author of the mod and give them this hash:\n"
            + detector.GetLastHashString(DllType::Game)
            + "\n\nIf you already know which version it is, set GameProfile in the [Game] section of the game ini. See the readme.");
        Screen::UpdateToOrigSize();
        return false;
    }
    }

    // Apply the game resolution now - must not happen during the menu (fixed size).
    Screen::ApplyGameResolution();

    ModuleInfo module = detector.GetModuleInfo(DllType::Game);
    module.version = version;   // may be the forced one; ModuleInfo::valid() checks it

    GameDllHooks::init(module.base);
    state.patchEngine.emplace(
        std::make_unique<MemoryRelocator>(),
        std::make_unique<CodePatcher>()
    );

    if (!state.patchEngine->Apply(module, profile->game(), state.patchSession))
    {
        GameDllHooks::shutdown();
        state.patchEngine.reset();

        GetUIFilter().setEnabled(true);

        ShowErrorAsync("Couldn't patch game dll due to some error. Contact the author.");
        Screen::UpdateToOrigSize();
        return false;
    }

    Zoom::GetState().setMode(verifiedFusion ? Screen::GetZoomMode() : Zoom::Mode::Off);
    return true;
}

bool UninstallGamePatches(TargetState& state)
{
    Zoom::GetState().setMode(Zoom::Mode::Off);

    if (state.active)
    {
        state.patchSession.Unapply();
        state.patchEngine.reset();
    }

    GameDllHooks::shutdown();

    GetUIFilter().setEnabled(true);

    return true;
}

bool InstallMenuPatches(TargetState& state, uintptr_t base, size_t size, const std::wstring& path)
{
    std::thread([] { AudioHelper::EnsureMaxVolume(); }).detach();

    DllVersionDetector& detector = DllVersionDetector::GetInstance();
    GameVersion version = detector.GetOrDetectGameVersion(DllType::Menu, path, base, size);
    DetectionStatus status = detector.GetDetectionStatus(DllType::Menu);

    // "[Game] MenuProfile=" forces a profile onto a dll we couldn't identify. Only once the
    // file was read and hashed, otherwise ModuleInfo has nothing to patch against.
    if (status == DetectionStatus::Supported || status == DetectionStatus::UnsupportedHash)
    {
        const GameVersion forced = ProfileOverride::GetProfileOverride(DllType::Menu);
        if (forced != GameVersion::UNKNOWN)
        {
            version = forced;
            status = DetectionStatus::Supported;
        }
    }

    ProfileFactory factory;
    auto profile = factory.create(version);

    // Every menu profile is cosmetic - a splash text hook, plus erasing the old HD mod's
    // on-screen text. Skipping it on an unrecognized dll costs nothing and gates nothing,
    // so this stays silent: no dialog, just no menu patches.
    switch (status)
    {
    case DetectionStatus::NotDetected:
    case DetectionStatus::NotCalculated:
    case DetectionStatus::UnsupportedHash:
        return false;
    case DetectionStatus::Supported:
    {
        // I check profile version separately, because dll can be identified, but there can be no profile for this version
        if (profile->isUnknown())
            return false;
        break;
    }
    }

    ModuleInfo module = detector.GetModuleInfo(DllType::Menu);
    module.version = version;   // may be the forced one; ModuleInfo::valid() checks it

    MenuDllHooks::init(module.base);
    state.patchEngine.emplace(
        std::make_unique<MemoryRelocator>(),
        std::make_unique<CodePatcher>()
    );

    if (!state.patchEngine->Apply(module, profile->menu(), state.patchSession))
    {
        MenuDllHooks::shutdown();
        state.patchEngine.reset();

        ShowErrorAsync("Couldn't patch menu dll due to some error. Contact the author.");
        return false;
    }

    return true;
}

bool UninstallMenuPatches(TargetState& state)
{
    if (state.active)
    {
        state.patchSession.Unapply();
        state.patchEngine.reset();
    }

    MenuDllHooks::shutdown();

    return true;
}

// DllMonitor searches each target's name part inside the loaded module's file name and
// stops at the first hit, so a configured name that already contains a built-in part is
// covered by that one and must not be registered twice.
static bool MatchesAnyTarget(const std::vector<TargetInfo>& targets, const std::wstring& moduleName)
{
    std::wstring lower = moduleName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    for (const TargetInfo& target : targets)
    {
        std::wstring part = target.namePart;
        std::transform(part.begin(), part.end(), part.begin(), ::towlower);

        if (!part.empty() && lower.find(part) != std::wstring::npos)
            return true;
    }

    return false;
}

std::vector<TargetInfo> CreatePatchTargets()
{
    std::vector<TargetInfo> targets;
    
    // Default Sudden Strike dll files with "game" and "menu" in its names
    {
        TargetInfo gameTarget;
        gameTarget.namePart = ToDllName(DllType::Game);
        gameTarget.onLoaded = InstallGamePatches;
        gameTarget.onUnloaded = UninstallGamePatches;
        targets.push_back(std::move(gameTarget));

        TargetInfo menuTarget;
        menuTarget.namePart = ToDllName(DllType::Menu);
        menuTarget.onLoaded = InstallMenuPatches;
        menuTarget.onUnloaded = UninstallMenuPatches;
        targets.push_back(std::move(menuTarget));
    }

    // Confrontation: Gulf War
    {
        TargetInfo gameTargetGulfWar;
        gameTargetGulfWar.namePart = ToDllName(DllType::GameGulfWar);
        gameTargetGulfWar.onLoaded = InstallGamePatches;
        gameTargetGulfWar.onUnloaded = UninstallGamePatches;
        targets.push_back(std::move(gameTargetGulfWar));

        TargetInfo menuTargetGulfWar;
        menuTargetGulfWar.namePart = ToDllName(DllType::MenuGulfWar);
        menuTargetGulfWar.onLoaded = InstallMenuPatches;
        menuTargetGulfWar.onUnloaded = UninstallMenuPatches;
        targets.push_back(std::move(menuTargetGulfWar));
    }

    // Confrontation: Black Gold
    {
        TargetInfo gameTargetBlackGold;
        gameTargetBlackGold.namePart = ToDllName(DllType::GameBlackGold);
        gameTargetBlackGold.onLoaded = InstallGamePatches;
        gameTargetBlackGold.onUnloaded = UninstallGamePatches;
        targets.push_back(std::move(gameTargetBlackGold));

        TargetInfo menuTargetBlackGold;
        menuTargetBlackGold.namePart = ToDllName(DllType::MenuBlackGold);
        menuTargetBlackGold.onLoaded = InstallMenuPatches;
        menuTargetBlackGold.onUnloaded = UninstallMenuPatches;
        targets.push_back(std::move(menuTargetBlackGold));
    }

    // Confrontation: Europe 2015
    {
        TargetInfo gameTargetEurope2015;
        gameTargetEurope2015.namePart = ToDllName(DllType::GameEurope2015);
        gameTargetEurope2015.onLoaded = InstallGamePatches;
        gameTargetEurope2015.onUnloaded = UninstallGamePatches;
        targets.push_back(std::move(gameTargetEurope2015));

        TargetInfo menuTargetEurope2015;
        menuTargetEurope2015.namePart = ToDllName(DllType::MenuEurope2015);
        menuTargetEurope2015.onLoaded = InstallMenuPatches;
        menuTargetEurope2015.onUnloaded = UninstallMenuPatches;
        targets.push_back(std::move(menuTargetEurope2015));
    }

    // Confrontation: Black Sea
    {
        TargetInfo gameTargetBlackSea;
        gameTargetBlackSea.namePart = ToDllName(DllType::GameBlackSea);
        gameTargetBlackSea.onLoaded = InstallGamePatches;
        gameTargetBlackSea.onUnloaded = UninstallGamePatches;
        targets.push_back(std::move(gameTargetBlackSea));

        TargetInfo menuTargetBlackSea;
        menuTargetBlackSea.namePart = ToDllName(DllType::MenuBlackSea);
        menuTargetBlackSea.onLoaded = InstallMenuPatches;
        menuTargetBlackSea.onUnloaded = UninstallMenuPatches;
        targets.push_back(std::move(menuTargetBlackSea));
    }

    // Whatever "[StartUp] Module1/Module2" actually names, for the installs where that is
    // not a "menu*"/"game*" file - a replacement front end such as RUI.dll. Added last, so
    // a stock install matches its built-in part first and nothing changes there.
    {
        const auto addConfigured = [&targets](DllType type, auto onLoaded, auto onUnloaded)
        {
            std::wstring name = GameModules::GetConfiguredName(type);
            if (name.empty() || MatchesAnyTarget(targets, name))
                return;

            TargetInfo configured;
            configured.namePart = std::move(name);
            configured.onLoaded = onLoaded;
            configured.onUnloaded = onUnloaded;
            targets.push_back(std::move(configured));
        };

        addConfigured(DllType::Menu, InstallMenuPatches, UninstallMenuPatches);
        addConfigured(DllType::Game, InstallGamePatches, UninstallGamePatches);
    }

    return targets;
}
