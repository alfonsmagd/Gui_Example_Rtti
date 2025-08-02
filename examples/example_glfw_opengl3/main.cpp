// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <vector>
#include <string>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

#include <json.hpp>
#include <iostream>

using json = nlohmann::json;

#define TEXTURE_VIEW_SIZE  ImVec2(300, 300) // Size of the texture view in pixels

static void glfw_error_callback( int error, const char* description )
{
    fprintf( stderr, "GLFW Error %d: %s\n", error, description );
}

GLuint GenerateCheckerTexture(int color)
{
    const int size = 164;
    auto* pixels = new unsigned char[size * size * 4];

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            int i = (y * size + x) * 4;
            bool checker = ((x / 8) % 2) ^ ((y / 8) % 2);
            unsigned char c = checker ? color : 100;
            pixels[i + 0] = c;
            pixels[i + 1] = c;
            pixels[i + 2] = c;
            pixels[i + 3] = 255;
        }
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    delete[] pixels;
    return texID;
}


namespace ReflectiveJson
{
   

    // ---------- Type name helper ----------
    template<typename T>
    std::string getTypeName()
    {
        std::string raw = typeid(T).name();
        #if defined(__clang__) || defined(__GNUC__)
        int status;
        char* demangled = abi::__cxa_demangle( raw.c_str(), nullptr, nullptr, &status );
        std::string result = (status == 0) ? demangled : raw;
        std::free( demangled );
        return result;
        #else
        if( raw.rfind( "struct ", 0 ) == 0 ) return raw.substr( 7 );
        if( raw.rfind( "class ", 0 ) == 0 ) return raw.substr( 6 );
        return raw;
        #endif
    }

    // ---------- Reflection checker ----------
    template<typename T>
    concept HasGetFields = requires { T::getFields(); };

    template<typename T>
    constexpr bool has_getFields_v = HasGetFields<T>;

    // ---------- FieldInfo ----------
    template<typename T>
    struct FieldInfo
    {
        const char* name;
        std::string typeName;
        std::function<std::string( const T& )> getter;
        std::function<void( T& )> drawGui;
        std::optional<std::pair<int, int>> intRange = std::nullopt;
        std::optional<std::pair<float, float>> floatRange = std::nullopt;
    };

    // ---------- Macros simplificadas ----------
    #define RTTI_FIELDS_BEGIN(CLASS) \
    static const std::vector<ReflectiveJson::FieldInfo<CLASS>>& getFields() { \
        using CurrentClass = CLASS; \
        static const std::vector<ReflectiveJson::FieldInfo<CLASS>> fields = {

    #define RTTI_FIELDS_END() \
        }; return fields; }

    #define RTTI_FIELD(FIELD) \
    ReflectiveJson::FieldInfo<CurrentClass>{ \
        #FIELD, \
        ReflectiveJson::getTypeName<decltype(CurrentClass::FIELD)>(), \
        [](const CurrentClass& self) { \
            using FieldType = decltype(self.FIELD); \
            if constexpr (std::is_same_v<FieldType, bool>) \
                return self.FIELD ? "true" : "false"; \
            else if constexpr (std::is_same_v<FieldType, std::string>) \
                return "\"" + self.FIELD + "\""; \
            else if constexpr (std::is_arithmetic_v<FieldType>) \
                return std::to_string(self.FIELD); \
            else \
                return "{object}"; \
        }, \
        [](CurrentClass& self) { \
            ImGui::PushID(#FIELD); \
            using FieldType = decltype(self.FIELD); \
            if constexpr (ReflectiveJson::has_getFields_v<FieldType>) { \
                if (ImGui::CollapsingHeader(#FIELD)) \
                    ReflectiveJson::DrawImGui(self.FIELD, true); \
            } else if constexpr (std::is_same_v<FieldType, int>) { \
                ImGui::DragInt(#FIELD, &self.FIELD); \
            } else if constexpr (std::is_same_v<FieldType, float>) { \
                ImGui::DragFloat(#FIELD, &self.FIELD, 0.1f); \
            } else if constexpr (std::is_same_v<FieldType, bool>) { \
                ImGui::Checkbox(#FIELD, &self.FIELD); \
            } else if constexpr (std::is_same_v<FieldType, ImVec2>) { \
                ImGui::DragFloat2(#FIELD, (float*)&self.FIELD); \
            } else if constexpr (std::is_same_v<FieldType, ImVec4>) { \
                ImGui::ColorEdit4(#FIELD, (float*)&self.FIELD, \
                    ImGuiColorEditFlags_DisplayRGB | \
                    ImGuiColorEditFlags_PickerHueBar | \
                    ImGuiColorEditFlags_AlphaBar); \
            } else if constexpr (std::is_same_v<FieldType, ImTextureID>) { \
                ImGui::Text("%s", #FIELD); \
                if (self.FIELD) { \
                    ImGui::Image(self.FIELD, TEXTURE_VIEW_SIZE); \
                } else { \
                    ImGui::TextDisabled("Texture not visible or null"); \
                } \
            } else if constexpr (std::is_same_v<FieldType, std::string>) { \
                char buffer[256]; \
                strncpy(buffer, self.FIELD.c_str(), sizeof(buffer)); \
                buffer[sizeof(buffer) - 1] = '\0'; \
                if (ImGui::InputText(#FIELD, buffer, sizeof(buffer))) { \
                    self.FIELD = buffer; \
                } \
            } \
            ImGui::PopID(); \
        } \
    }


    #define RTTI_FIELD_WITH_RANGE(FIELD, MIN, MAX) \
    ReflectiveJson::FieldInfo<CurrentClass>{ \
        #FIELD, \
        ReflectiveJson::getTypeName<decltype(CurrentClass::FIELD)>(), \
        [](const CurrentClass& self) { return std::to_string(self.FIELD); }, \
        [](CurrentClass& self) { \
            ImGui::PushID(#FIELD); \
            if constexpr (std::is_same_v<decltype(self.FIELD), int>) { \
                ImGui::SliderInt(#FIELD, &self.FIELD, MIN, MAX); \
            } else if constexpr (std::is_same_v<decltype(self.FIELD), float>) { \
                ImGui::SliderFloat(#FIELD, &self.FIELD, MIN, MAX); \
            } else { \
                ImGui::Text("Unsupported ranged type"); \
            } \
            ImGui::PopID(); \
        }, \
        std::is_same_v<decltype(CurrentClass::FIELD), int> ? std::make_optional(std::make_pair(MIN, MAX)) : std::nullopt, \
        std::is_same_v<decltype(CurrentClass::FIELD), float> ? std::make_optional(std::make_pair(MIN, MAX)) : std::nullopt \
    }

    // ---------- ImGui drawer ----------
    template<typename T>
    void DrawImGui( T& obj, bool skipHeader = false )
    {
        std::string label = ReflectiveJson::getTypeName<T>();
        if( label.empty() ) label = "Unnamed";

        std::string headerId = label + "##" + std::to_string( reinterpret_cast< uintptr_t >(&obj) );

        if( skipHeader || ImGui::CollapsingHeader( headerId.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::PushID( &obj );
            ImGui::Indent();
            for( const auto& field : T::getFields() )
            {
                if( field.drawGui ) field.drawGui( obj );
            }
            ImGui::Unindent();
            ImGui::PopID();
        }
    }

    // ---------- JSON Serializer ----------
    template<typename T>
    json toJson( const T& obj )
    {
        json j;
        j[ "type" ] = getTypeName<T>();
        for( const auto& field : T::getFields() )
        {
            j[ field.name ][ "type" ] = field.typeName;
            j[ field.name ][ "value" ] = field.getter( obj );
        }
        return j;
    }
} // namespace ReflectiveJson

  // ---------- DrawImGui recursivo ----------


//template<typename T>
//void DrawImGui(T& obj) {
//    const char* typeName = typeid(T).name(); 
//    if (ImGui::CollapsingHeader(typeName)) {
//        for (auto& field : T::getFields()) {
//            if (field.drawGui)
//                field.drawGui(obj);
//        }
//    }
//}

struct Stats
{
    int strength;
    float agility;

    RTTI_FIELDS_BEGIN( Stats )
        RTTI_FIELD_WITH_RANGE( strength, 0, 100 ),
        RTTI_FIELD_WITH_RANGE( agility, 0.0f, 10.0f )
        RTTI_FIELDS_END()
};

struct Player
{
    std::string name;
    bool alive;
    Stats stats;

    RTTI_FIELDS_BEGIN( Player )
        RTTI_FIELD( name ),
        RTTI_FIELD( alive ),
        RTTI_FIELD( stats )
        RTTI_FIELDS_END()
};

struct Specular
{
    float r;
    float g;
    float b;
    int a;

    RTTI_FIELDS_BEGIN( Specular )
        RTTI_FIELD_WITH_RANGE( r, 0.0f, 1.0f ),
        RTTI_FIELD_WITH_RANGE( g, 0.0f, 1.0f ),
        RTTI_FIELD_WITH_RANGE( b, 0.0f, 1.0f )
        RTTI_FIELDS_END()
};

struct Emissive
{
    float r;
    float g;
    float b;
    int a;

    RTTI_FIELDS_BEGIN( Emissive )
        RTTI_FIELD_WITH_RANGE( r, 0.0f, 10.0f ),
        RTTI_FIELD_WITH_RANGE( g, 0.0f, 10.0f ),
        RTTI_FIELD_WITH_RANGE( b, 0.0f, 10.0f )
        RTTI_FIELDS_END()
};

struct Roughness
{
    float value;

    RTTI_FIELDS_BEGIN( Roughness )
        RTTI_FIELD_WITH_RANGE( value, 0.0f, 1.0f )
        RTTI_FIELDS_END()
};

struct Metallic
{
    float value;

    RTTI_FIELDS_BEGIN( Metallic )
        RTTI_FIELD_WITH_RANGE( value, 0.0f, 1.0f )
        RTTI_FIELDS_END()
};

struct Light
{

    ImVec4 color;
    float intensity;
    RTTI_FIELDS_BEGIN( Light )
        RTTI_FIELD( color ),
        RTTI_FIELD_WITH_RANGE( intensity, 0.0f, 100.0f )
        RTTI_FIELDS_END()
};

struct Material
{
    Specular specular;
    Emissive emissive;
    Roughness roughness;
    Metallic metallic;
    Player owner;

    RTTI_FIELDS_BEGIN( Material )
        RTTI_FIELD( specular ),
        RTTI_FIELD( emissive ),
        RTTI_FIELD( roughness ),
        RTTI_FIELD( metallic ),
        RTTI_FIELD( owner )
        RTTI_FIELDS_END()
};



struct  GFrameBuffer
{
    ImTextureID positionTex;
    ImTextureID normalTex;
    ImTextureID depthTex;

    RTTI_FIELDS_BEGIN( GFrameBuffer )
        RTTI_FIELD( positionTex ),
        RTTI_FIELD( normalTex ),
        RTTI_FIELD( depthTex )
        RTTI_FIELDS_END()


};









// Main code
int main( int, char** )
{

    //Player player;

    Material material{};
    Light    light{};
    //auto j =  ReflectiveJson::toJson( material );
    // std::cout << j.dump(4) << std::endl; // bonito con indentación
    //






    glfwSetErrorCallback( glfw_error_callback );
    if( !glfwInit() )
        return 1;

    // Decide GL+GLSL versions
    #if defined(IMGUI_IMPL_OPENGL_ES2)
        // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 2 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 0 );
    glfwWindowHint( GLFW_CLIENT_API, GLFW_OPENGL_ES_API );
    #elif defined(IMGUI_IMPL_OPENGL_ES3)
        // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 0 );
    glfwWindowHint( GLFW_CLIENT_API, GLFW_OPENGL_ES_API );
    #elif defined(__APPLE__)
        // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 2 );
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );  // 3.2+ only
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE );            // Required on Mac
    #else
        // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 0 );
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
    #endif

        // Create window with graphics context
    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor( glfwGetPrimaryMonitor() ); // Valid on GLFW 3.3+ only
    GLFWwindow* window = glfwCreateWindow( ( int )(1280 * main_scale), ( int )(800 * main_scale), "Dear ImGui GLFW+OpenGL3 example", nullptr, nullptr );
    if( window == nullptr )
        return 1;
    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 ); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); ( void )io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes( main_scale );        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale * 1.5;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL( window, true );
    #ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks( window, "#canvas" );
    #endif
    ImGui_ImplOpenGL3_Init( glsl_version );

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    io.Fonts->AddFontFromFileTTF( "../../misc/fonts/Roboto-Medium.ttf" );
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);
    // Our state
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4( 0.45f, 0.55f, 0.60f, 1.00f );

    style = ImGui::GetStyle();
    main_scale = 1.2f; // You can adjust this value for bigger/smaller UI
    style.ScaleAllSizes( main_scale );

    // Main loop
    #ifdef __EMSCRIPTEN__
        // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
        // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
        #else

    //Simulate a texture for demonstration purposes
    GFrameBuffer gFrameBuffer;
    gFrameBuffer.positionTex = (ImTextureID)(uintptr_t)GenerateCheckerTexture(255);
    gFrameBuffer.normalTex = (ImTextureID)(uintptr_t)GenerateCheckerTexture(123);
    gFrameBuffer.depthTex = (ImTextureID)(uintptr_t)GenerateCheckerTexture(22);
    while( !glfwWindowShouldClose( window ) )
        #endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();
        if( glfwGetWindowAttrib( window, GLFW_ICONIFIED ) != 0 )
        {
            ImGui_ImplGlfw_Sleep( 10 );
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if( show_demo_window )
            ImGui::ShowDemoWindow( &show_demo_window );

        //Albedo albedo{};
        ReflectiveJson::DrawImGui( material ); 
        Stats stats{};
        ReflectiveJson::DrawImGui( stats ); 

        ReflectiveJson::DrawImGui( light ); 

        ReflectiveJson::DrawImGui( gFrameBuffer ); 

      

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin( "Hello, world!" );                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text( "This is some useful text." );               // Display some text (you can use a format strings too)
            ImGui::Checkbox( "Demo Window", &show_demo_window );      // Edit bools storing our window open/close state
            ImGui::Checkbox( "Another Window", &show_another_window );

            ImGui::SliderFloat( "float", &f, 0.0f, 1.0f );            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3( "clear color", ( float* )&clear_color ); // Edit 3 floats representing a color

            if( ImGui::Button( "Button" ) )                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text( "counter = %d", counter );

            ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate );



            // Define the component state
            struct MyComponent
            {
                char text[ 128 ] = "";
                int combo_current = 0;
                bool submitted = false;
            };

            // Place these static variables at the top of your main loop (inside ImGui::Begin)
            static std::vector<MyComponent> components;
            static const char* combo_items[] = { "Option 1", "Option 2", "Option 3" };

            // Button to add a new component
            if( ImGui::Button( "Add Component" ) )
            {
                components.push_back( MyComponent() );
            }

            // Render all components
            for( size_t i = 0; i < components.size(); ++i )
            {
                ImGui::Separator();
                ImGui::Text( "Component #%zu", i + 1 );

                ImGui::InputText( ("Text##" + std::to_string( i )).c_str(), components[ i ].text, IM_ARRAYSIZE( components[ i ].text ) );
                ImGui::Combo( ("Combo##" + std::to_string( i )).c_str(), &components[ i ].combo_current, combo_items, IM_ARRAYSIZE( combo_items ) );

                if( ImGui::Button( ("Submit##" + std::to_string( i )).c_str() ) )
                {
                    components[ i ].submitted = true;
                    printf( "Component %zu: Text='%s', Combo='%s'\n", i + 1, components[ i ].text, combo_items[ components[ i ].combo_current ] );
                }
                if( components[ i ].submitted )
                    ImGui::TextColored( ImVec4( 0, 1, 0, 1 ), "Submitted!" );
            }




            ImGui::End();
        }

        // 3. Show another simple window.
        if( show_another_window )
        {
            ImGui::Begin( "Another Window", &show_another_window );   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text( "Hello from another window!" );
            if( ImGui::Button( "Close Me" ) )
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize( window, &display_w, &display_h );
        glViewport( 0, 0, display_w, display_h );
        glClearColor( clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w );
        glClear( GL_COLOR_BUFFER_BIT );
        ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );

        glfwSwapBuffers( window );
    }
    #ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
    #endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow( window );
    glfwTerminate();

    return 0;
}
