// *****************************************************************************
    // This is a minimal, Emscripten-only entry point for the web build.
    // Unlike Main.cpp (the desktop emulator's entry point), this has no
    // ImGui-based settings/menu UI, no settings/controls persistence, and
    // no command-line cartridge argument: the BIOS and one cartridge are
    // both preloaded into the Emscripten virtual filesystem at build time
    // (see the --preload-file flags in build_web.sh) and loaded from fixed
    // paths below.

    // include console logic headers
    #include "ConsoleLogic/V32Console.hpp"

    // include infrastructure headers
    #include "DesktopInfrastructure/Logger.hpp"
    #include "DesktopInfrastructure/FilePaths.hpp"

    // include emulator headers
    #include "EmulatorControl.hpp"
    #include "GamepadsInput.hpp"
    #include "VideoOutput.hpp"
    #include "AudioOutput.hpp"
    #include "Globals.hpp"
    #include "StopWatch.hpp"

    // include C/C++ headers
    #include <iostream>         // [ C++ STL ] I/O Streams
    #include <cstddef>          // [ ANSI C ] Standard definitions
    #include <cctype>           // [ ANSI C ] Character classification

    // include SDL2 headers
    #define SDL_MAIN_HANDLED
    #include "SDL.h"            // [ SDL2 ] Main header

    // include Emscripten headers
    #include <emscripten.h>

    // declare used namespaces
    using namespace std;
    using namespace V32;
// *****************************************************************************


// =============================================================================
//      FIXED PATHS FOR THE WEB BUILD
// =============================================================================


// these files are placed at these exact virtual paths by build_web.sh's
// --preload-file arguments
const char BiosPath[] = "/data/Bios/StandardBios.v32";
const char CartridgePath[] = "/data/cartridge.v32";


// =============================================================================
//      MEMORY CARD PATH
// =============================================================================


// Mounted onto IndexedDB (persists across reloads/browser restarts) by
// persist_storage.js's preRun hook, before main() ever runs. Not a fixed
// compile-time path: it's derived there from location.pathname, so that
// separate game deployments under the same domain (different
// subdirectories, same origin - e.g. mygames.com/gameA/ vs.
// mygames.com/gameB/) each get a genuinely separate IndexedDB database
// rather than sharing one - confirmed empirically that a fixed path
// would NOT have this property (IndexedDB is scoped per origin, not per
// path, and a fixed mount path maps directly to one fixed database name
// regardless of which page mounts it). Read once at startup since it
// depends on the browser's JS environment, not anything known at
// compile time.
EM_JS( char*, GetPersistMountPathJS, (), {
    return stringToNewUTF8( Module['persistMountPath'] );
});

// One card per game, named after the cartridge's own title rather than
// its (always-the-same, "/data/cartridge.v32") file path - belt-and-
// suspenders alongside the per-deployment mount path above: harmless if
// a deployment only ever serves one game (the normal case), and still
// prevents collisions in the unusual case of two different games
// sharing one deployment/mount path with different titles.
string GetMemoryCardPath()
{
    char* PersistDirRaw = GetPersistMountPathJS();
    string PersistDir = PersistDirRaw;
    free( PersistDirRaw );

    string Title = Console.GetCartridgeTitle();
    string SanitizedTitle;

    for( char c : Title )
      SanitizedTitle += isalnum( (unsigned char)c ) ? c : '_';

    if( SanitizedTitle.empty() )
      SanitizedTitle = "card";

    return PersistDir + "/" + SanitizedTitle + ".memc";
}


// =============================================================================
//      BASIC ABI ASSERTIONS
// =============================================================================


// (identical to Main.cpp's own version: this check is not specific to any
// particular platform, so it is simply duplicated here rather than shared,
// to keep this file self-contained and the desktop entry point untouched)
void PerformABIAssertions()
{
    LOG( "Performing ABI assertions" );
    V32Word TestWord = {0};

    // determine the correct packing sizes
    if( sizeof(V32Word) != 4 )
      THROW( "ABI check failed: Vircon words are not 4 bytes in size" );

    // determine the correct bit endianness: instructions
    TestWord.AsInstruction.OpCode = 0x1;

    if( TestWord.AsBinary != 0x04000000 )
      THROW( "ABI check failed: Fields of CPU instructions are not correctly ordered" );

    // determine the correct byte endianness
    TestWord.AsColor.R = 0x11;
    TestWord.AsColor.G = 0x22;
    TestWord.AsColor.B = 0x33;
    TestWord.AsColor.A = 0x44;

    if( TestWord.AsBinary != 0x44332211 )
      THROW( "ABI check failed: Components GPU colors are not correctly ordered as RGBA" );
}

// -----------------------------------------------------------------------------

void PerformPortAssertions()
{
    LOG( "Performing I/O port assertions" );
    V32GPU TestGPU;

    // determine the correct location of ports
    int DetectedGPUPortDistance = (int)((V32Word*)(&TestGPU.DrawingAngle) - (V32Word*)(&TestGPU.Command));
    int ExpectedGPUPortDistance = ((int)GPU_LocalPorts::DrawingAngle - (int)GPU_LocalPorts::Command);

    if( DetectedGPUPortDistance != ExpectedGPUPortDistance )
      THROW( "Port check failed: GPU ports are not correctly ordered, or there is padding between them" );
}


// =============================================================================
//      MINIMAL REPLACEMENT FOR SetDefaultSettings() (Settings.cpp)
// =============================================================================


// Settings.cpp's version pulls in GUI.hpp just for SetWindowZoom1X(); this
// avoids that whole dependency chain (and ImGui) for a one-line call
void SetDefaultSettingsWeb()
{
    Video.SetWindowZoom( 1 );

    Audio.SetMute( false );
    Audio.SetOutputVolume( 1.0 );

    Console.UnloadCartridge();
    Console.UnloadMemoryCard();

    // set keyboard for first gamepad
    Console.SetGamepadConnection( 0, true );
    Gamepads.MappedGamepads[ 0 ].Type = DeviceTypes::Keyboard;

    // no device for the rest of gamepads (no joystick support in this build)
    for( int i = 1; i < Constants::GamepadPorts; i++ )
    {
        Console.SetGamepadConnection( i, false );
        Gamepads.MappedGamepads[ i ].Type = DeviceTypes::NoDevice;
    }

    Emulator.SetCardHandling( false );
}


// =============================================================================
//      MAIN LOOP (called once per animation frame by the browser)
// =============================================================================


// state that used to be local variables inside Main.cpp's main(); moved to
// file scope because the web build's "main" registers this callback and
// returns immediately, instead of blocking in its own loop
StopWatch Watch;
float PendingFrames = 1;


void WebMainLoopStep()
{
    // process window/input events
    SDL_Event Event;

    while( SDL_PollEvent( &Event ) )
    {
        if( Event.type == SDL_QUIT )
        {
            GlobalLoopActive = false;
        }

        Gamepads.ProcessEvent( Event );
    }

    // redirect all rendering to emulator's display
    Video.RenderToFramebuffer();
    Video.BeginFrame();

    // measure cycle time
    double TimeStep = Watch.GetStepTime();

    if( Emulator.IsPowerOn() && !Emulator.IsPaused() )
    {
        // execute frames as needed
        PendingFrames += TimeStep * 60.0;

        // Cap how many frames can be run in a single callback. Without
        // this, a single slow frame (far more likely here than on the
        // native desktop build - WebGL/software rendering, a busy tab,
        // GC pauses, a heavier-drawing game) makes PendingFrames pile up,
        // and this loop tries to run all of them back-to-back to "catch
        // up". If that catch-up itself takes longer than the shortfall
        // (very plausible under the same conditions that caused it),
        // the next callback has an even bigger TimeStep to catch up on -
        // an accelerating loop that can consume the entire JS thread
        // inside one requestAnimationFrame callback and never yield back
        // to the browser, which looks exactly like a hung tab. Dropping
        // the excess instead just means the game briefly runs in slow
        // motion after a stall, rather than the tab locking up.
        const float MaxFramesPerCallback = 5;
        if( PendingFrames > MaxFramesPerCallback )
          PendingFrames = MaxFramesPerCallback;

        while( PendingFrames >= 0.9 )
        {
            Emulator.RunNextFrame();
            PendingFrames -= 1;
        }
    }

    // show the emulator's display on screen
    Video.RenderToScreen();

    if( Emulator.IsPowerOn() )
      Video.DrawFramebufferOnScreen();
    else
      Video.ClearScreen( GPUColor{ 0, 0, 0, 255 } );

    // show updates on screen
    SDL_GL_SwapWindow( Video.GetWindow() );

    // Push the same CPU/GPU load figures the native desktop build's own
    // ImGui overlay reads (V32Console::GetCPULoad()/GetGPULoad(), each a
    // 0-100 percentage of that frame's cycle/pixel budget - see
    // V32Console.cpp) out to the page's own JS side once per real frame.
    // No native ImGui here (this build deliberately excludes it, see this
    // file's own top comment), so the actual overlay/graph rendering lives
    // in JS instead (shell.html) - this call is just the data hand-off.
    if( Emulator.IsPowerOn() )
    {
        float CpuLoad = Console.GetCPULoad();
        float GpuLoad = Console.GetGPULoad();

        EM_ASM(
        {
            if( Module.onPerfUpdate ) Module.onPerfUpdate( $0, $1 );
        }, CpuLoad, GpuLoad );
    }

    // Periodically push the memory card file to IndexedDB, rather than
    // every single frame - Module.persistMemoryCard() (see
    // persist_storage.js) triggers an async IndexedDB write, and there's
    // no reason to do that dozens of times a second. Worst case with this
    // interval, a hard crash/tab close loses the last ~2 seconds of card
    // writes - persist_storage.js's "pagehide" listener also attempts one
    // last save on a normal tab close, on top of this.
    //
    // This used to be gated on Console.WasMemoryCardModified(), but that
    // flag is also read (and cleared) every single frame inside
    // V32Console::RunNextFrame() itself, which saves the card to the
    // in-memory virtual file and immediately clears the flag as soon as
    // any write happens - all within that same frame. By the time this
    // periodic check ran, the flag had therefore already been cleared and
    // was never observed as true here, so persistMemoryCard() was never
    // actually called after the initial blank-card creation: the virtual
    // file itself stayed correctly up to date, but that update was never
    // pushed to IndexedDB, so it did not survive a reload. Persisting
    // unconditionally on this timer (instead of trying to detect changes
    // from outside RunNextFrame()) is what actually keeps saves durable.
    static int FramesSinceLastCardCheck = 0;

    if( ++FramesSinceLastCardCheck >= 120 )
    {
        FramesSinceLastCardCheck = 0;

        if( Console.HasMemoryCard() )
          EM_ASM( { if( Module['persistMemoryCard'] ) Module['persistMemoryCard'](); } );
    }
}


// =============================================================================
//      MAIN FUNCTION
// =============================================================================


int main( int NumberOfArguments, char* Arguments[] )
{
    (void)NumberOfArguments;
    (void)Arguments;

    try
    {
        LOG_TO_FILE( "/DebugLog" );

        PerformABIAssertions();
        PerformPortAssertions();

        InitializeGlobalVariables();

        // init SDL
        LOG( "Initializing SDL" );

        Uint32 SDLSubsystems =
        (
            SDL_INIT_VIDEO      |
            SDL_INIT_AUDIO      |
            SDL_INIT_TIMER      |
            SDL_INIT_EVENTS
        );

        if( SDL_Init( SDLSubsystems ) != 0 )
          THROW( string("Cannot initialize SDL: ") + SDL_GetError() );

        // we need to create a window/canvas for SDL to receive any events
        Video.CreateOpenGLWindow();

        // log graphic device info
        string GraphicDeviceVendor = (const char *)glGetString( GL_VENDOR );
        string GraphicDeviceModel = (const char *)glGetString( GL_RENDERER );

        LOG( "Graphic device vendor: " + GraphicDeviceVendor );
        LOG( "Graphic device model: " + GraphicDeviceModel );

        // initialize OpenGL shaders and their infrastructure
        Video.InitRendering();

        // create a framebuffer object
        Video.CreateFramebuffer();
        Video.RenderToScreen();

        // set alpha blending
        LOG( "Enabling alpha blending" );
        glEnable( GL_BLEND );
        Video.SetBlendingMode( IOPortValues::GPUBlendingMode_Alpha );

        // (audio is initialized below, by Emulator.Initialize() itself)

        // set our default settings
        SetDefaultSettingsWeb();

        // -----------------------------------------------------------------------------

        // turn on Vircon VM
        Emulator.Initialize();

        // load the standard bios (preloaded into the virtual filesystem)
        Console.LoadBios( BiosPath );

        // load the (single, preloaded) cartridge and power on
        Console.LoadCartridge( CartridgePath );
        Emulator.SetPower( true );

        // load (or create, on this game's first run) its memory card,
        // from the IndexedDB-backed persistent directory persist_storage.js
        // already mounted and synced before this point
        string CardPath = GetMemoryCardPath();

        if( !FileExists( CardPath ) )
        {
            Console.CreateMemoryCard( CardPath );

            // persist the freshly-created blank card immediately, rather
            // than waiting for the periodic WasMemoryCardModified() check
            // in WebMainLoopStep() - that only tracks changes to an
            // already-loaded card, so a session that closes before ever
            // reaching that check (e.g. a game that never actually writes
            // save data) would otherwise lose this initial creation
            // entirely, and see no card at all again next time
            EM_ASM( { if( Module['persistMemoryCard'] ) Module['persistMemoryCard'](); } );
        }

        Console.LoadMemoryCard( CardPath );

        // -----------------------------------------------------------------------------

        LOG( "---------------------------------------------------------------------" );
        LOG( "    Starting main loop" );
        LOG( "---------------------------------------------------------------------" );
        GlobalLoopActive = true;

        // hand control over to the browser: it will call WebMainLoopStep()
        // once per animation frame from now on. The "1" (simulate infinite
        // loop) means execution never returns past this call.
        emscripten_set_main_loop( WebMainLoopStep, 0, 1 );
    }

    catch( const exception& e )
    {
        LOG( "ERROR: " + string(e.what()) );
        cerr << "FATAL ERROR: " << e.what() << endl;
    }

    return 0;
}
