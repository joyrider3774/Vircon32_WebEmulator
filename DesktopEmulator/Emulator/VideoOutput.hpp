// *****************************************************************************
    // start include guard
    #ifndef VIDEOOUTPUT_HPP
    #define VIDEOOUTPUT_HPP
    
    // include common Vircon headers
    #include "../VirconDefinitions/DataStructures.hpp"
    #include "../VirconDefinitions/Enumerations.hpp"
    
    // include console logic headers
    #include "ConsoleLogic/ExternalInterfaces.hpp"
    
    // include SDL2 headers
    #define SDL_MAIN_HANDLED
    #include "SDL.h"            // [ SDL2 ] Main header
    
    // include OpenGL headers
    #if defined(__EMSCRIPTEN__)
      // Targets ES2/WebGL1 rather than ES3/WebGL2: WebGL2 support is
      // markedly less mature in WebKit-based browsers (Safari, GNOME Web)
      // than WebGL1, and has been reported to cause flickering/black
      // screens there. The handful of ES3-only calls this file used
      // (VAOs, glBlitFramebuffer, glDrawBuffers, sized texture formats)
      // are guarded out under __EMSCRIPTEN__ elsewhere in VideoOutput.cpp
      // with ES2-compatible equivalents - see CreateFramebuffer(),
      // CompileShaderProgram(), and DrawFramebufferOnScreen().
      #include <GLES2/gl2.h>    // [ OpenGL ] WebGL1-compatible GLES2 headers (Emscripten provides the symbols directly, no loader needed)
    #else
      #include <glad/glad.h>      // [ OpenGL ] GLAD Loader (already includes <GL/gl.h>)
    #endif
// *****************************************************************************


// we will render our quads in groups using a
// fixed size queue; this parameter sets the
// queue size and acts as group size limit
//
// Bigger under Emscripten specifically: every GL call here has to cross
// from compiled WASM into JS (WebGL only exists as a browser/JS API),
// and that crossing has real per-call overhead native desktop GL never
// pays - confirmed via CPU profiling, where the dominant cost during a
// real slowdown was Emscripten's JS/WASM call-boundary machinery itself
// (getWasmTableEntry, wasm-to-js, js-to-wasm), not game logic or GPU
// shader time. SelectTexture()/SetMultiplyColor()/SetBlendingMode() all
// force a flush on any state change regardless of queue size, so this
// only helps runs of same-texture quads longer than the old limit of 20 -
// it can't help texture-switch-forced flushes - but it's a strictly safe
// change either way: it only ever reduces how many separate draw calls a
// long same-texture run takes, never what ends up on screen. Memory cost
// is trivial even at this size (a few KB of vertex/index data).
#if defined(__EMSCRIPTEN__)
  #define QUAD_QUEUE_SIZE 512
#else
  #define QUAD_QUEUE_SIZE 20
#endif


// =============================================================================
//      2D-SPECIALIZED OPENGL CONTEXT
// =============================================================================


class VideoOutput
{
    private:
        
        // video context objects
        SDL_Window* Window;
        SDL_GLContext OpenGLContext;
        
        // graphical settings
        unsigned WindowWidth;
        unsigned WindowHeight;
        unsigned WindowedZoomFactor;
        
        // arrays to hold buffer info
        GLfloat QuadVerticesInfo[ 16 * QUAD_QUEUE_SIZE ];
        GLushort VertexIndices[ 6 * QUAD_QUEUE_SIZE ];
        
        // current color modifiers
        V32::GPUColor MultiplyColor;
        V32::IOPortValues BlendingMode;
        
        // OpenGL IDs of loaded textures
        GLuint BiosTextureID;
        GLuint CartridgeTextureIDs[ V32::Constants::GPUMaximumCartridgeTextures ];
        int32_t SelectedTexture;
        
        // white texture used to draw solid colors
        GLuint WhiteTextureID;
        
        // framebuffer object data
        GLuint FramebufferID;
        GLuint FBColorTextureID;
        unsigned FramebufferWidth;
        unsigned FramebufferHeight;
        
        // additional GL objects
        GLuint VAO;
        GLuint VBOVertexInfo;
        GLuint VBOIndices;
        GLuint ShaderProgramID;
        
        // rendering control for quad groups
        int QueuedQuads;
        
        // positions of shader parameters
        GLuint VertexInfoLocation;
        GLuint TextureUnitLocation;
        GLuint MultiplyColorLocation;
        
    public:
        
        // instance handling
        VideoOutput();
       ~VideoOutput();
        
        // init functions
        void CreateOpenGLWindow();
        void CreateFramebuffer();
        bool CompileShaderProgram();
        void CreateWhiteTexture();
        void InitRendering();
        
        // release functions
        void Destroy();
        
        // external context access
        SDL_Window* GetWindow();
        SDL_GLContext GetOpenGLContext();
        GLuint GetFramebufferID();
        
        // view configuration
        void SetWindowZoom( int ZoomFactor );
        int GetWindowZoom();
        float GetRelativeWindowWidth();
        
        // framebuffer render functions
        void RenderToScreen();
        void RenderToFramebuffer();
        void DrawFramebufferOnScreen();
        void BeginFrame();
        
        // color control functions
        void SetMultiplyColor( V32::GPUColor NewMultiplyColor );
        V32::GPUColor GetMultiplyColor();
        void SetBlendingMode( V32::IOPortValues BlendingMode );
        V32::IOPortValues GetBlendingMode();
        
        // render functions
        void ClearScreen( V32::GPUColor ClearColor );
        void AddQuadToQueue( const V32::GPUQuad& Quad );
        void RenderQuadQueue();
        
        // texture handling
        void LoadTexture( int GPUTextureID, void* Pixels );
        void UnloadTexture( int GPUTextureID );
        void SelectTexture( int GPUTextureID );
        int32_t GetSelectedTexture();
};


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
