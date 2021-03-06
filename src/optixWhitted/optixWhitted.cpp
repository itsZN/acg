#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <tuple>
#include <vector>

#ifdef __APPLE__
# include <GLUT/glut.h>
#else
# include <GL/glew.h>
# if defined( _WIN32 )
#  include <GL/wglew.h>
#  include <GL/freeglut.h>
# else
#  include <GL/glut.h>
# endif
#endif

#include <optixu/optixpp_namespace.h>
#include <optixu/optixu_math_stream_namespace.h>

#include <sutil.h>
#include "commonStructs.h"

using namespace optix;

const uint32_t width  = 768u;
const uint32_t height = 768u;

auto ptxPath(const std::string& cuda_file) -> std::string;
auto parse_obj_file(std::string path, Context &c)
    -> std::vector<GeometryInstance>;
auto create_context() -> Context;
auto create_scene(Context &context) -> GeometryInstance;
auto create_reflect_sphere(Context &context) -> GeometryInstance;
auto create_glass_sphere(Context &context) -> GeometryInstance;
void setup_lights(Context &context);
void setup_camera(Context &context);

void unused(void)
{
    system("/bin/sh");
}

void glutInitialize(int* argc, char** argv)
{
    std::cout << "[+] glutInitialize" << std::endl;
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_ALPHA | GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize(width, height);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("Toshi's Reverse Shell");
    glutHideWindow();
}

int main(int argc, char **argv)
{
    std::cout << "[+] main()" << std::endl;
    if (argc != 3) {
        std::cerr << argv[0] << " <input_obj> <output_file>" << std::endl; 
        return EXIT_FAILURE;
    }
    std::string input_obj = argv[1];
    std::string out_file  = argv[2];

    Context context;
    try {
        glutInitialize(&argc, argv);
        glewInit();
        context = create_context();

        // Create GIs for each piece of geometry
        //auto gis = parse_obj_file(std::move(input_obj), context);
        auto gis = std::vector<GeometryInstance>();
        gis.push_back(create_scene(context));
        gis.push_back(create_reflect_sphere(context));
        //gis.push_back(create_glass_sphere(context));

        // Place all in group
        GeometryGroup geometrygroup = context->createGeometryGroup();
        geometrygroup->setChildCount(static_cast<unsigned int>(gis.size()));
        for (unsigned int i = 0; i < gis.size(); ++i)
            geometrygroup->setChild(i, gis[i]);
        geometrygroup->setAcceleration( context->createAcceleration("NoAccel") );
        context["top_object"]->set(geometrygroup);
        context["top_shadower"]->set(geometrygroup);

        // setup lights, camera
        setup_lights(context);
        setup_camera(context);

        context->validate();
        context->launch(0, width, height);
        sutil::displayBufferPPM(out_file.c_str(),
                context["output_buffer"]->getBuffer());
        context->destroy();
    } SUTIL_CATCH( context->get() )
    return EXIT_SUCCESS;
}

auto create_reflect_sphere(Context &context) -> GeometryInstance
{
    // sphere
    const std::string sphere_ptx = ptxPath( "sphere.cu" );
    Geometry metal_sphere = context->createGeometry();
    metal_sphere->setPrimitiveCount( 1u );
    metal_sphere->setBoundingBoxProgram( context->createProgramFromPTXFile( sphere_ptx, "bounds" ) );
    metal_sphere->setIntersectionProgram( context->createProgramFromPTXFile( sphere_ptx, "robust_intersect" ) );
    metal_sphere["sphere"]->setFloat( 3.0f, 1.5f, -3.75f, 1.0f );

    // metal material
    const std::string metal_ptx = ptxPath( "toon.cu" );
    Program toon_ch = context->createProgramFromPTXFile(
            metal_ptx, "closest_hit_radiance");
    Program toon_ah = context->createProgramFromPTXFile(
            metal_ptx, "any_hit_shadow");
    Material metal_matl = context->createMaterial();
    metal_matl->setClosestHitProgram( 0, toon_ch );
    metal_matl->setAnyHitProgram( 1, toon_ah );
    metal_matl["Ka"]->setFloat( 0.2f, 0.5f, 0.5f );
    metal_matl["Kd"]->setFloat( 0.2f, 0.7f, 0.8f );
    //metal_matl["Kd"]->setFloat( 0.5f, 0.5f, 0.5f );
    metal_matl["Ks"]->setFloat( 0.9f, 0.9f, 0.9f );
    metal_matl["toon_exp"]->setFloat( 64 );

    //metal_matl["Kr"]->setFloat( 1.0f,  1.0f,  1.0f);
    metal_matl["Kr"]->setFloat( 0.0f,  0.0f,  0.0f);

    return context->createGeometryInstance(
            metal_sphere, &metal_matl, &metal_matl + 1);

}

auto create_glass_sphere(Context &context) -> GeometryInstance
{
    // Create glass sphere geometry
    const std::string shell_ptx = ptxPath( "sphere_shell.cu" );
    Geometry glass_sphere = context->createGeometry();
    glass_sphere->setPrimitiveCount( 1u );
    glass_sphere->setBoundingBoxProgram( context->createProgramFromPTXFile( shell_ptx, "bounds" ) );
    glass_sphere->setIntersectionProgram( context->createProgramFromPTXFile( shell_ptx, "intersect" ) );
    glass_sphere["center"]->setFloat( 4.0f, 2.3f, -4.0f );
    glass_sphere["radius1"]->setFloat( 0.96f );
    glass_sphere["radius2"]->setFloat( 1.0f );

    // Glass material
    const std::string glass_ptx = ptxPath( "glass.cu" );
    Program glass_ch = context->createProgramFromPTXFile( glass_ptx, "closest_hit_radiance" );
    Program glass_ah = context->createProgramFromPTXFile( glass_ptx, "any_hit_shadow" );
    Material glass_matl = context->createMaterial();
    glass_matl->setClosestHitProgram( 0, glass_ch );
    glass_matl->setAnyHitProgram( 1, glass_ah );

    glass_matl["importance_cutoff"]->setFloat( 1e-2f );
    glass_matl["cutoff_color"]->setFloat( 0.034f, 0.055f, 0.085f );
    glass_matl["fresnel_exponent"]->setFloat( 3.0f );
    glass_matl["fresnel_minimum"]->setFloat( 0.1f );
    glass_matl["fresnel_maximum"]->setFloat( 1.0f );
    glass_matl["refraction_index"]->setFloat( 1.4f );
    glass_matl["refraction_color"]->setFloat( 1.0f, 1.0f, 1.0f );
    glass_matl["reflection_color"]->setFloat( 1.0f, 1.0f, 1.0f );
    glass_matl["refraction_maxdepth"]->setInt( 10 );
    glass_matl["reflection_maxdepth"]->setInt( 5 );
    const float3 extinction = make_float3(.83f, .83f, .83f);
    glass_matl["extinction_constant"]->setFloat( log(extinction.x), log(extinction.y), log(extinction.z) );
    glass_matl["shadow_attenuation"]->setFloat( 0.6f, 0.6f, 0.6f );

    return context->createGeometryInstance(
            glass_sphere, &glass_matl, &glass_matl + 1);
}
    

auto create_triangle(Context &context,
                     const float3 &x,
                     const float3 &y,
                     const float3 &z) -> GeometryInstance
{
    Matrix4x4 scale;
    scale.setRow(0, make_float4(10.0, 0.0, 0.0, 0.0));
    scale.setRow(1, make_float4( 0.0,10.0, 0.0, 0.0));
    scale.setRow(2, make_float4( 0.0, 0.0,10.0, 0.0));
    scale.setRow(3, make_float4( 0.0, 0.0, 0.0, 1.0));

    Matrix4x4 trans;
    trans.setRow(0, make_float4( 1.0, 0.0, 0.0, 2.0));
    trans.setRow(1, make_float4( 0.0, 1.0, 0.0, 0.0));
    trans.setRow(2, make_float4( 0.0, 0.0, 1.0, -4.0));
    trans.setRow(3, make_float4( 0.0, 0.0, 0.0, 1.0));

    float rot = M_PI;
    Matrix4x4 rotm;
    rotm.setRow(0, make_float4(cos(rot), -sin(rot), 0, 0));
    rotm.setRow(1, make_float4(sin(rot), -cos(rot), 0, 0));
    rotm.setRow(2, make_float4(0, 0, 1, 0));
    rotm.setRow(3, make_float4(0, 0, 0, 1));


    // TODO: Optimize. Can we have a mesh geometry with many primitives?
    // This will allow us to use many more rays for shadow computation
    std::string triangle_ptx = ptxPath("triangle.cu");
    Geometry triangle = context->createGeometry();
    triangle->setPrimitiveCount(1u);
    triangle->setBoundingBoxProgram(
            context->createProgramFromPTXFile(triangle_ptx, "bounds"));
    triangle->setIntersectionProgram(
            context->createProgramFromPTXFile(triangle_ptx, "robust_intersect"));
    
    triangle["z"]->setFloat(make_float3(trans*rotm*scale*make_float4(x.x,x.y,x.z,1.0)));
    triangle["y"]->setFloat(make_float3(trans*rotm*scale*make_float4(y.x,y.y,y.z,1.0)));
    triangle["x"]->setFloat(make_float3(trans*rotm*scale*make_float4(z.x,z.y,z.z,1.0)));


    // metal material
    const std::string metal_ptx = ptxPath( "toon.cu" );
    Program toon_ch = context->createProgramFromPTXFile(
            metal_ptx, "closest_hit_radiance");
    Program toon_ah = context->createProgramFromPTXFile(
            metal_ptx, "any_hit_shadow");
    Material metal_matl = context->createMaterial();
    metal_matl->setClosestHitProgram( 0, toon_ch );
    metal_matl->setAnyHitProgram( 1, toon_ah );
    metal_matl["Ka"]->setFloat( 0.2f, 0.5f, 0.5f );
    metal_matl["Kd"]->setFloat( 0.2f, 0.7f, 0.8f );
    metal_matl["Ks"]->setFloat( 0.9f, 0.9f, 0.9f );
    metal_matl["toon_exp"]->setFloat( 64 );

    metal_matl["Kr"]->setFloat( 0.0f,  0.0f,  0.0f);


    //metal_matl["Kr"]->setFloat( 0.0f,  0.0f,  0.0f);

    return context->createGeometryInstance(triangle, &metal_matl, &metal_matl+1);
}

auto parse_obj_file(std::string path, Context &c) -> std::vector<GeometryInstance>
{
    std::cout << "[+] parse_obj_file" << std::endl;
    // parse the obj file
    std::ifstream obj(path.c_str());
    if (!obj) {
        std::cout << "[-] could not find file " << path << std::endl;
        throw std::runtime_error("Could not open file");
    }
    auto triangles = std::vector<GeometryInstance>();
    auto vertices  = std::vector<float3>();

    std::string op;
    while (obj >> op) {
        if (op == "v") {
            float x, y, z;
            obj >> x >> y >> z;
            vertices.push_back(make_float3(x, y, z));
        } else if (op == "f") {
            unsigned int x, y, z;
            obj >> x >> y >> z;
            triangles.push_back(create_triangle(c, vertices[x-1],
                                                   vertices[y-1],
                                                   vertices[z-1]));
        }
    }
    return triangles;
}

auto ptxPath(const std::string& cuda_file) -> std::string
{
    return std::string(sutil::samplesPTXDir()) +
            "/optixWhitted_generated_" + cuda_file + ".ptx";
}

auto create_context() -> Context
{
    std::cout << "[+] create_context" << std::endl;
    Context context = Context::create();
    context->setRayTypeCount(2);
    context->setEntryPointCount(1);
    context->setStackSize(2800);

    context["max_depth"]->setInt( 10 );
    context["radiance_ray_type"]->setUint( 0 );
    context["shadow_ray_type"]->setUint( 1 );
    context["distance_ray_type"]->setUint( 2 );
    context["frame"]->setUint( 0u );
    context["scene_epsilon"]->setFloat( 1.e-4f );
    context["ambient_light_color"]->setFloat( 0.4f, 0.4f, 0.4f );

    // construct png output buffer
    Buffer output = sutil::createOutputBuffer(context, RT_FORMAT_UNSIGNED_BYTE4,
                                              width, height, false);
    Buffer accum  = context->createBuffer(RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL,
                                          RT_FORMAT_FLOAT4, width, height);
    context["output_buffer"]->set(output);
    context["accum_buffer"]->set(accum);

    // Ray generation program
    std::string ptx_path(ptxPath("accum_camera.cu"));
    Program ray_gen_program = context->createProgramFromPTXFile(ptx_path, "pinhole_camera");
    context->setRayGenerationProgram(0, ray_gen_program);

    // Exception program
    Program exception_program = context->createProgramFromPTXFile(ptx_path, "exception");
    context->setExceptionProgram(0, exception_program);
    context["bad_color"]->setFloat(1.0f, 0.0f, 1.0f);
    // I heard online this was useful
    context->setPrintEnabled(true);
    context->setPrintBufferSize(1024);

    // Miss program
    ptx_path = ptxPath("constantbg.cu");
    context->setMissProgram(0, context->createProgramFromPTXFile(ptx_path, "miss"));
    context["bg_color"]->setFloat(0.34f, 0.55f, 0.85f);

    return context;
}

auto create_scene(Context &context) -> GeometryInstance
{
    // setup floor
    std::cout << "[+] create_scene" << std::endl;
    auto floor_ptx = ptxPath("parallelogram.cu");
    auto parallelogram = context->createGeometry();
    parallelogram->setPrimitiveCount(1u);
    parallelogram->setBoundingBoxProgram(
            context->createProgramFromPTXFile(floor_ptx, "bounds"));
    parallelogram->setIntersectionProgram(
            context->createProgramFromPTXFile(floor_ptx, "intersect"));
    float3 anchor = make_float3( -16.0f, 0.01f, -8.0f );
    float3 v1 = make_float3( 32.0f, 0.0f, 0.0f );
    float3 v2 = make_float3( 0.0f, 0.0f, 16.0f );
    float3 normal = cross( v1, v2 );
    normal = normalize( normal );
    float d = dot( normal, anchor );
    v1 *= 1.0f/dot( v1, v1 );
    v2 *= 1.0f/dot( v2, v2 );
    float4 plane = make_float4( normal, d );
    parallelogram["plane"]->setFloat( plane );
    parallelogram["v1"]->setFloat( v1 );
    parallelogram["v2"]->setFloat( v2 );
    parallelogram["anchor"]->setFloat( anchor );

    // Checker material for floor
    const std::string checker_ptx = ptxPath( "checker.cu" );
    Program check_ch = context->createProgramFromPTXFile( checker_ptx, "closest_hit_radiance" );
    Program check_ah = context->createProgramFromPTXFile( checker_ptx, "any_hit_shadow" );
    Material floor_matl = context->createMaterial();
    floor_matl->setClosestHitProgram( 0, check_ch );
    floor_matl->setAnyHitProgram( 1, check_ah );

    floor_matl["Kd1"]->setFloat( 0.8f, 0.3f, 0.15f);
    floor_matl["Ka1"]->setFloat( 0.8f, 0.3f, 0.15f);
    floor_matl["Ks1"]->setFloat( 0.0f, 0.0f, 0.0f);
    floor_matl["Kd2"]->setFloat( 0.9f, 0.85f, 0.05f);
    floor_matl["Ka2"]->setFloat( 0.9f, 0.85f, 0.05f);
    floor_matl["Ks2"]->setFloat( 0.0f, 0.0f, 0.0f);
    floor_matl["inv_checker_size"]->setFloat( 32.0f, 16.0f, 1.0f );
    floor_matl["toon_exp1"]->setFloat( 0.0f );
    floor_matl["toon_exp2"]->setFloat( 0.0f );
    floor_matl["Kr1"]->setFloat( 0.0f, 0.0f, 0.0f);
    floor_matl["Kr2"]->setFloat( 0.0f, 0.0f, 0.0f);

    return context->createGeometryInstance(
            parallelogram, &floor_matl, &floor_matl + 1);
}

void setup_lights(Context &context)
{
    BasicLight lights[] = {
        { make_float3( 60.0f, 40.0f, 0.0f ), make_float3( 1.0f, 1.0f, 1.0f ), 1 }
    };

    Buffer light_buffer = context->createBuffer(RT_BUFFER_INPUT);
    light_buffer->setFormat(RT_FORMAT_USER);
    light_buffer->setElementSize(sizeof(BasicLight));
    light_buffer->setSize(sizeof(lights)/sizeof(lights[0]));
    memcpy(light_buffer->map(), lights, sizeof(lights));
    light_buffer->unmap();

    context["lights"]->set(light_buffer);
}

void setup_camera(Context &context)
{
    const float vfov  = 60.0f;
    const float aspect_ratio = static_cast<float>(width) /
        static_cast<float>(height);

    float3 camera_eye         = make_float3(8.0f, 2.0f, -4.0f);
    float3 camera_lookat      = make_float3(4.0f, 2.3f, -4.0f);
    float3 camera_up          = make_float3(0.0f, 1.0f,  0.0f);
    Matrix4x4 camera_rotate   = Matrix4x4::identity();

    float3 camera_u, camera_v, camera_w;
    sutil::calculateCameraVariables(
            camera_eye, camera_lookat, camera_up,
            vfov, aspect_ratio, camera_u, camera_v, camera_w, true);

    const Matrix4x4 frame = Matrix4x4::fromBasis(
            normalize( camera_u),
            normalize( camera_v),
            normalize(-camera_w),
            camera_lookat);
    const Matrix4x4 frame_inv = frame.inverse();
    // Apply camera rotation twice to match old SDK behavior
    const Matrix4x4 trans   = frame*camera_rotate*camera_rotate*frame_inv;

    camera_eye    = make_float3(trans*make_float4(camera_eye, 1.0f));
    camera_lookat = make_float3(trans*make_float4(camera_lookat, 1.0f));
    camera_up     = make_float3(trans*make_float4(camera_up, 0.0f));

    sutil::calculateCameraVariables(
            camera_eye, camera_lookat, camera_up,
            vfov, aspect_ratio, camera_u, camera_v, camera_w, true);
    camera_rotate = Matrix4x4::identity();

    context["eye"]->setFloat(camera_eye);
    context["U"  ]->setFloat(camera_u);
    context["V"  ]->setFloat(camera_v);
    context["W"  ]->setFloat(camera_w);
}
