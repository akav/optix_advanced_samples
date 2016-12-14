/* 
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//-----------------------------------------------------------------------------
//
// optixGlass: a glass shader example
//
//-----------------------------------------------------------------------------

#ifndef __APPLE__
#  include <GL/glew.h>
#  if defined( _WIN32 )
#    include <GL/wglew.h>
#  endif
#endif

#include <GLFW/glfw3.h>

#include <optixu/optixpp_namespace.h>
#include <optixu/optixu_math_stream_namespace.h>

#include <sutil.h>
#include "commonStructs.h"
#include <Arcball.h>
#include <OptiXMesh.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdint.h>

using namespace optix;

const char* const SAMPLE_NAME = "optixGlass";
const unsigned int WIDTH  = 768u;
const unsigned int HEIGHT = 576u;

//------------------------------------------------------------------------------
//
// Globals
//
//------------------------------------------------------------------------------

Context      context = 0;
GLFWwindow*  g_window = 0; 
unsigned int g_accumulation_frame = 0;

// Camera state for raygen program
struct Camera
{
    Camera( unsigned int width, unsigned int height, 
            const float3& camera_eye,
            const float3& camera_lookat,
            const float3& camera_up,
            Variable eye, Variable u, Variable v, Variable w) 

        : m_width( width ),
        m_height( height ),
        m_camera_eye( camera_eye ),
        m_camera_lookat( camera_lookat ),
        m_camera_up( camera_up ),
        m_camera_rotate( Matrix4x4::identity() ),
        m_variable_eye( eye ),
        m_variable_u( u ),
        m_variable_v( v ),
        m_variable_w( w )
        {
            apply();
        }

  // Compute derived uvw frame and write to OptiX context
    void apply( )
    {
        const float vfov  = 45.0f;
        const float aspect_ratio = static_cast<float>(m_width) /
            static_cast<float>(m_height);

        float3 camera_u, camera_v, camera_w;
        sutil::calculateCameraVariables(
                m_camera_eye, m_camera_lookat, m_camera_up, vfov, aspect_ratio,
                camera_u, camera_v, camera_w, /*fov_is_vertical*/ true );

        const Matrix4x4 frame = Matrix4x4::fromBasis(
                normalize( camera_u ),
                normalize( camera_v ),
                normalize( -camera_w ),
                m_camera_lookat);
        const Matrix4x4 frame_inv = frame.inverse();
        // Apply camera rotation twice to match old SDK behavior
        const Matrix4x4 trans   = frame*m_camera_rotate*m_camera_rotate*frame_inv;

        m_camera_eye    = make_float3( trans*make_float4( m_camera_eye,    1.0f ) );
        m_camera_lookat = make_float3( trans*make_float4( m_camera_lookat, 1.0f ) );
        m_camera_up     = make_float3( trans*make_float4( m_camera_up,     0.0f ) );

        sutil::calculateCameraVariables(
                m_camera_eye, m_camera_lookat, m_camera_up, vfov, aspect_ratio,
                camera_u, camera_v, camera_w, true );

        m_camera_rotate = Matrix4x4::identity();

        // Write variables to OptiX context
        m_variable_eye->setFloat( m_camera_eye );
        m_variable_u->setFloat( camera_u );
        m_variable_v->setFloat( camera_v );
        m_variable_w->setFloat( camera_w );
    }

    bool process_mouse( float x, float y, bool left_button_down, bool right_button_down )
    {
        static sutil::Arcball arcball;
        static float2 mouse_prev_pos = make_float2( 0.0f, 0.0f );
        static bool   have_mouse_prev_pos = false;

        bool dirty = false;

        if ( left_button_down || right_button_down ) {
            if ( have_mouse_prev_pos ) {
                if ( left_button_down ) {

                    const float2 from = { mouse_prev_pos.x, mouse_prev_pos.y };
                    const float2 to   = { x, y };

                    const float2 a = { from.x / m_width, from.y / m_height };
                    const float2 b = { to.x   / m_width, to.y   / m_height };

                    m_camera_rotate = arcball.rotate( b, a );

                } else if ( right_button_down ) {
                    const float dx = ( x - mouse_prev_pos.x ) / m_width;
                    const float dy = ( y - mouse_prev_pos.y ) / m_height;
                    const float dmax = fabsf( dx ) > fabs( dy ) ? dx : dy;
                    const float scale = fminf( dmax, 0.9f );

                    m_camera_eye = m_camera_eye + (m_camera_lookat - m_camera_eye)*scale;

                }

                apply();
                dirty = true;

            }

            have_mouse_prev_pos = true;
            mouse_prev_pos.x = x;
            mouse_prev_pos.y = y;

        } else {
            have_mouse_prev_pos = false;
        }

        return dirty;
    }

    bool resize( unsigned int w, unsigned int h) {
        if ( w == m_width && h == m_height) return false;
        m_width = w;
        m_height = h;
        apply();
        return true;
    }

    unsigned int width() const  { return m_width; }
    unsigned int height() const { return m_height; }

    private:
    unsigned int m_width;
    unsigned int m_height;

    float3    m_camera_eye;
    float3    m_camera_lookat;
    float3    m_camera_up;
    Matrix4x4 m_camera_rotate;

    // Handles for setting derived values for OptiX context
    Variable  m_variable_eye;
    Variable  m_variable_u;
    Variable  m_variable_v;
    Variable  m_variable_w;

};

Camera* g_camera = NULL;


//------------------------------------------------------------------------------
//
//  Helper functions
//
//------------------------------------------------------------------------------
    

static void errorCallback(int error, const char* description)                   {                                                                                
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;           
}


static std::string ptxPath( const std::string& cuda_file )
{
    return
        std::string(sutil::samplesPTXDir()) +
        "/" + std::string(SAMPLE_NAME) + "_generated_" +
        cuda_file +
        ".ptx";
}


static Buffer getOutputBuffer()
{
    return context[ "output_buffer" ]->getBuffer();
}


void destroyContext()
{
    if( context )
    {
        context->destroy();
        context = 0;
    }
}


void createContext( bool use_pbo )
{
    // Set up context
    context = Context::create();
    context->setRayTypeCount( 1 );
    context->setEntryPointCount( 1 );
    context->setStackSize( 2800 );

    // Note: high max depth for reflection and refraction through glass
    context["max_depth"]->setInt( 10 );
    context["radiance_ray_type"]->setUint( 0 );
    context["frame"]->setUint( 0u );
    context["scene_epsilon"]->setFloat( 1.e-3f );

    Buffer buffer = sutil::createOutputBuffer( context, RT_FORMAT_UNSIGNED_BYTE4, WIDTH, HEIGHT, use_pbo );
    context["output_buffer"]->set( buffer );

    // Accumulation buffer
    Buffer accum_buffer = context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL,
            RT_FORMAT_FLOAT4, WIDTH, HEIGHT );
    context["accum_buffer"]->set( accum_buffer );

    // Ray generation program
    std::string ptx_path( ptxPath( "accum_camera.cu" ) );
    Program ray_gen_program = context->createProgramFromPTXFile( ptx_path, "pinhole_camera" );
    context->setRayGenerationProgram( 0, ray_gen_program );

    // Exception program
    Program exception_program = context->createProgramFromPTXFile( ptx_path, "exception" );
    context->setExceptionProgram( 0, exception_program );
    context["bad_color"]->setFloat( 1.0f, 0.0f, 1.0f );

    // Miss program
    ptx_path = ptxPath( "gradientbg.cu" );
    context->setMissProgram( 0, context->createProgramFromPTXFile( ptx_path, "miss" ) );
    context["background_light"]->setFloat( 1.0f, 1.0f, 1.0f );
    context["background_dark"]->setFloat( 0.3f, 0.3f, 0.3f );

    // align background's up direction with camera's look direction
    float3 bg_up = normalize( make_float3(-14.0f, -14.0f, -7.0f) );

    // tilt the background's up direction in the direction of the camera's up direction
    bg_up.y += 1.0f;
    bg_up = normalize(bg_up);
    context["up"]->setFloat( bg_up.x, bg_up.y, bg_up.z );
}


Material createMaterial( const float3& extinction )
{
    const std::string ptx_path = ptxPath( "glass.cu" );
    Program ch_program = context->createProgramFromPTXFile( ptx_path, "closest_hit_radiance" );

    Material material = context->createMaterial();
    material->setClosestHitProgram( 0, ch_program );

    material["importance_cutoff"  ]->setFloat( 0.01f );
    material["cutoff_color"       ]->setFloat( 0.2f, 0.2f, 0.2f );
    material["fresnel_exponent"   ]->setFloat( 4.0f );
    material["fresnel_minimum"    ]->setFloat( 0.1f );
    material["fresnel_maximum"    ]->setFloat( 1.0f );
    material["refraction_index"   ]->setFloat( 1.4f );
    material["refraction_color"   ]->setFloat( 0.99f, 0.99f, 0.99f );
    material["reflection_color"   ]->setFloat( 0.99f, 0.99f, 0.99f );
    material["refraction_maxdepth"]->setInt( 10 );
    material["reflection_maxdepth"]->setInt( 5 );

    // Set this on the global context so it's easy to change in the gui
    context["extinction_constant"]->setFloat( log(extinction.x), log(extinction.y), log(extinction.z) );

    return material;
}


void createGeometry(
        const std::vector<std::string>& filenames,
        const std::vector<optix::Matrix4x4>& xforms, Material material
        )
{

    const std::string ptx_path = ptxPath( "triangle_mesh_iterative.cu" );

    GeometryGroup geometry_group = context->createGeometryGroup();
    for (size_t i = 0; i < filenames.size(); ++i) {

        OptiXMesh mesh;
        mesh.context = context;
        
        // override defaults
        mesh.intersection = context->createProgramFromPTXFile( ptx_path, "mesh_intersect" );
        mesh.bounds = context->createProgramFromPTXFile( ptx_path, "mesh_bounds" );
        mesh.material = material;

        loadMesh( filenames[i], mesh, xforms[i] ); 
        geometry_group->addChild( mesh.geom_instance );
    }

    geometry_group->setAcceleration( context->createAcceleration( "Trbvh" ) );

    context[ "top_object"   ]->set( geometry_group ); 
}






//------------------------------------------------------------------------------
//
//  GLFW callbacks
//
//------------------------------------------------------------------------------

void keyCallback( GLFWwindow* window, int key, int scancode, int action, int mods )
{
    bool handled = false;

    if( action == GLFW_PRESS )
    {
        switch( key )
        {
            case GLFW_KEY_Q:
            case GLFW_KEY_ESCAPE:
                if( context )
                    context->destroy();
                if( g_window )
                    glfwDestroyWindow( g_window );
                glfwTerminate();
                exit(EXIT_SUCCESS);

            case( GLFW_KEY_S ):
            {
                const std::string outputImage = std::string(SAMPLE_NAME) + ".ppm";
                std::cerr << "Saving current frame to '" << outputImage << "'\n";
                sutil::displayBufferPPM( outputImage.c_str(), getOutputBuffer() );
                handled = true;
                break;
            }
        }
    }

    if (!handled) {
        // forward key event to imgui
        ImGui_ImplGlfw_KeyCallback( window, key, scancode, action, mods );
    }
}

void windowSizeCallback( GLFWwindow* window, int w, int h )
{
    if (w < 0 || h < 0) return;

    const unsigned width = (unsigned)w;
    const unsigned height = (unsigned)h;

    if ( g_camera->resize( width, height ) ) {
        g_accumulation_frame = 0;
    }

    sutil::resizeBuffer( getOutputBuffer(), width, height );
    sutil::resizeBuffer( context[ "accum_buffer" ]->getBuffer(), width, height );

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);
    glViewport(0, 0, width, height);
}


//------------------------------------------------------------------------------
//
// GLFW setup and run 
//
//------------------------------------------------------------------------------

void glfwInitialize( )
{
    g_window = sutil::initGLFW();

    // Note: this overrides imgui key callback with our own.  We'll chain this.
    glfwSetKeyCallback( g_window, keyCallback );

    glfwSetWindowSize( g_window, (int)WIDTH, (int)HEIGHT );
    glfwSetWindowSizeCallback( g_window, windowSizeCallback );
}


void glfwRun()
{
    // Initialize GL state
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1 );
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, WIDTH, HEIGHT );

    unsigned int frame_count = 0;
    float3 glass_extinction = make_float3( 1.0f, 1.0f, 1.0f );
    int max_depth = 10;

    while( !glfwWindowShouldClose( g_window ) )
    {

        glfwPollEvents();                                                        

        ImGui_ImplGlfw_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        
        // Let imgui process the mouse first
        if (!io.WantCaptureMouse) {

            double x, y;
            glfwGetCursorPos( g_window, &x, &y );

            if ( g_camera->process_mouse( (float)x, (float)y, ImGui::IsMouseDown(0), ImGui::IsMouseDown(1) ) ) {
                g_accumulation_frame = 0;
            }
        }

        // imgui pushes
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(0,0) );
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,          0.6f        );
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f        );

        sutil::displayFps( frame_count++ );

        {
            static const ImGuiWindowFlags window_flags = 
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar;

            ImGui::SetNextWindowPos( ImVec2( 2.0f, 40.0f ) );
            ImGui::Begin("extinction", 0, window_flags );
            if (ImGui::SliderFloat3( "extinction", (float*)(&glass_extinction.x), 0.01f, 1.0f )) {
                context["extinction_constant"]->setFloat( log(glass_extinction.x), log(glass_extinction.y), log(glass_extinction.z) );
                g_accumulation_frame = 0;
            }
            ImGui::End();

            ImGui::SetNextWindowPos( ImVec2( 2.0f, 80.0f ) );
            ImGui::Begin("max depth", 0, window_flags );
            if (ImGui::SliderInt( "max depth", &max_depth, 1, 10 )) {
                context["max_depth"]->setInt( max_depth );
                g_accumulation_frame = 0;
            }
            ImGui::End();
        }

        // imgui pops
        ImGui::PopStyleVar( 3 );

        // Render main window
        context["frame"]->setUint( g_accumulation_frame++ );
        context->launch( 0, g_camera->width(), g_camera->height() );
        sutil::displayBufferGL( getOutputBuffer() );

        // Render gui over it
        ImGui::Render();

        glfwSwapBuffers( g_window );
    }
    
    destroyContext();
    glfwDestroyWindow( g_window );
    glfwTerminate();
}


//------------------------------------------------------------------------------
//
// Main
//
//------------------------------------------------------------------------------

void printUsageAndExit( const std::string& argv0 )
{
    std::cerr << "\nUsage: " << argv0 << " [options]\n";
    std::cerr <<
        "App Options:\n"
        "  -h | --help              Print this usage message and exit.\n"
        "  -f | --file              Save single frame to file and exit.\n"
        "  -n | --nopbo             Disable GL interop for display buffer.\n"
        "  -m | --mesh <mesh_file>  Specify path to mesh to be loaded.\n"
        "App Keystrokes:\n"
        "  q  Quit\n"
        "  s  Save image to '" << SAMPLE_NAME << ".ppm'\n"
        << std::endl;

    exit(1);
}


int main( int argc, char** argv )
{
    bool use_pbo  = true;
    std::string out_file;
    std::vector<std::string> mesh_files;
    std::vector<optix::Matrix4x4> mesh_xforms;
    for( int i=1; i<argc; ++i )
    {
        const std::string arg( argv[i] );

        if( arg == "-h" || arg == "--help" )
        {
            printUsageAndExit( argv[0] );
        }
        else if( arg == "-f" || arg == "--file"  )
        {
            if( i == argc-1 )
            {
                std::cerr << "Option '" << arg << "' requires additional argument.\n";
                printUsageAndExit( argv[0] );
            }
            out_file = argv[++i];
        }
        else if ( arg == "-m" || arg == "--mesh" )
        {
            if( i == argc-1 )
            {
                std::cerr << "Option '" << arg << "' requires additional argument.\n";
                printUsageAndExit( argv[0] );
            }
            mesh_files.push_back( argv[++i] );
            mesh_xforms.push_back( optix::Matrix4x4::identity() );
        }
        else if( arg == "-n" || arg == "--nopbo"  )
        {
            use_pbo = false;
        }
        else
        {
            std::cerr << "Unknown option '" << arg << "'\n";
            printUsageAndExit( argv[0] );
        }
    }

    try
    {
        glfwInitialize();

#ifndef __APPLE__
        GLenum err = glewInit();
        if (err != GLEW_OK)
        {
            std::cerr << "GLEW init failed: " << glewGetErrorString( err ) << std::endl;
            exit(EXIT_FAILURE);
        }
#endif

        createContext( use_pbo );

        Material material = createMaterial( make_float3(1.0f, 1.0f, 1.0f) );

        if ( mesh_files.empty() ) {

            // Default scene

            mesh_files.push_back( std::string( sutil::samplesDir() ) + "/data/cognacglass.obj" );
            mesh_xforms.push_back( optix::Matrix4x4::translate( make_float3( 0.0f, 0.0f, -5.0f ) ) );

            mesh_files.push_back( std::string( sutil::samplesDir() ) + "/data/wineglass.obj" );
            mesh_xforms.push_back( optix::Matrix4x4::identity() );

            mesh_files.push_back( std::string( sutil::samplesDir() ) + "/data/waterglass.obj" );
            mesh_xforms.push_back( optix::Matrix4x4::translate( make_float3( -5.0f, 0.0f, 0.0f ) ) );
        }

        createGeometry( mesh_files, mesh_xforms, material );

        // Note: lighting comes from miss program

        context->validate();

        g_camera = new Camera( WIDTH, HEIGHT, 
                make_float3( 14.0f, 14.0f, 14.0f ),  //eye
                make_float3( 0.0f, 7.0f, 0.0f ),     //lookat
                make_float3( 0.0f, 1.0f,  0.0f ),    //up
                context["eye"], context["U"], context["V"], context["W"] );

        if ( out_file.empty() )
        {
            glfwRun();
        }
        else
        {
            // Accumulate a few frames for anti-aliasing
            for ( unsigned int frame = 0; frame < 10; ++frame ) {
                context["frame"]->setUint( frame );
                context->launch( 0, WIDTH, HEIGHT );
            }
            sutil::displayBufferPPM( out_file.c_str(), getOutputBuffer() );
            destroyContext();
        }
        delete g_camera; g_camera = NULL;
        return 0;
    }
    SUTIL_CATCH( context->get() )
}

