// OpenBFME. GPL-3.0.
// GDExtension entry point: registers the device-layer classes.

#include "GodotDevice/GodotAptPlayer.h"
#include "GodotDevice/GodotGameAudio.h"
#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotInGameHud.h"
#include "GodotDevice/GodotFXPlayer.h"
#include "GodotDevice/GodotLogicClock.h"
#include "GodotDevice/GodotMapObjectBuilder.h"
#include "GodotDevice/GodotMapTerrain.h"
#include "GodotDevice/GodotPathfindView.h"
#include "GodotDevice/GodotPostEffects.h"
#include "GodotDevice/GodotRelease.h"
#include "Common/ConsoleFilter.h"
#include "Common/WindowsCrashDump.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotTextureOverrides.h"
#include "GodotDevice/GodotVideoStream.h"
#include "GodotDevice/GodotW3DModelBuilder.h"
#include "GodotDevice/LuaGDExtensionRegister.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include "GodotDevice/GodotIOSTouch.h"
#else
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#endif
#endif

using namespace godot;

#if defined(__APPLE__) && !TARGET_OS_IOS
static void configure_initial_window()
{
	if (Engine::get_singleton()->is_editor_hint()) return;
	// OpenBFME presentation infrastructure. Godot 4.7 registers its early core
	// singletons before this callback, then reads display/window/size/mode before
	// creating/showing the native window (main/main.cpp, register_core_extensions).
	// Reading this in ios_startup.gd is too late: macOS has already shown a small
	// window and must move it into a fullscreen Space.
	const PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
	if (args.has("--display-check") || args.has("--display-game")) return;
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load("user://openbfme-display.cfg") != OK) return; // normal display setup reports malformed files
	const Variant value = config->get_value("display", "window_mode", "windowed");
	if (value.get_type() != Variant::STRING) return; // normal display setup reports invalid values
	const String mode = value;
	int initial = DisplayServer::WINDOW_MODE_WINDOWED;
	if (mode == "borderless") initial = DisplayServer::WINDOW_MODE_FULLSCREEN;
	else if (mode == "fullscreen") initial = DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN;
	else if (mode != "windowed") return;
	ProjectSettings::get_singleton()->set_setting("display/window/size/mode", initial);
}
#endif

static void initialize_openbfme(ModuleInitializationLevel p_level)
{
#if defined(__APPLE__) && !TARGET_OS_IOS
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) configure_initial_window();
#endif
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		return;
	}
	GDREGISTER_CLASS(RetailFileSystem);
	GDREGISTER_CLASS(W3DModelBuilder);
	GDREGISTER_CLASS(MapTerrainBuilder);
	GDREGISTER_CLASS(MapObjectBuilder);
	GDREGISTER_CLASS(PathfindView);
	GDREGISTER_CLASS(W3DInstancer);
	GDREGISTER_CLASS(LogicClock);
	GDREGISTER_CLASS(AptMenuPlayer);
	GDREGISTER_CLASS(GameWorld);
	GDREGISTER_CLASS(InGameHudNode); // HUD-1
	GDREGISTER_CLASS(FXPlayer); // FX-1
	GDREGISTER_CLASS(GameAudio);
	GDREGISTER_CLASS(GammaCompositeEffect); // RENDER-3 (S-831)
	GDREGISTER_CLASS(GammaCompositeHost);
	GDREGISTER_CLASS(LookupTablePostEffect); // RENDER-4 (S-1650)
	GDREGISTER_CLASS(MapPostEffectsHost);
	GDREGISTER_CLASS(ReleaseInfo); // RELEASE-1
	GDREGISTER_CLASS(SessionLogger);
	GDREGISTER_CLASS(InstallSetup);
	GDREGISTER_CLASS(VP6MovieStream); // CAMP-2
	LuaGDExtension::initialize(); // LUAGD-1: LuaState, LuaTable, ... and Lua as a script language
#if defined(__APPLE__) && TARGET_OS_IOS
	OpenBFMEIOSTouch::install();
#endif
}

static void uninitialize_openbfme(ModuleInitializationLevel p_level)
{
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
	{
#if defined(__APPLE__) && TARGET_OS_IOS
		OpenBFMEIOSTouch::uninstall();
#endif
		LuaGDExtension::uninitialize(); // LUAGD-1
		WindowsCrashDump::uninstall();
		ConsoleFilter::uninstall(); // RELEASE-1: the original stdout / stderr back before the extension's code goes
		GammaComposite::shutdown();
		W3D_Clear_Texture_Overrides();
	}
}

extern "C"
{
GDExtensionBool GDE_EXPORT openbfme_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
	GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
{
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_openbfme);
	init_obj.register_terminator(uninitialize_openbfme);
#if defined(__APPLE__) && !TARGET_OS_IOS
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_CORE);
#else
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
#endif
	return init_obj.init();
}
}
