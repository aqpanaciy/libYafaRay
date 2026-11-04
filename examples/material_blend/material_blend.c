/* Simple C90 client: a sphere blending matte paint and mirror through a wavy grayscale mask, rendered by the installed DLL.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <yafaray_c_api.h>

#define WIDTH 800
#define HEIGHT 600

typedef struct RenderOptions
{
    const char *output;
    int aa_samples;
    int path_samples;
    int light_samples;
    int threads;
    int ray_depth;
    double transition_width;
    double wave_amplitude;
} RenderOptions;

static void usage(const char *program)
{
    printf("Usage: %s [output.tga|output.png] [options]\n"
           "  --aa-samples N     Film AA_minsamples (default: 32)\n"
           "  --path-samples N   Integrator path_samples (default: 8)\n"
           "  --light-samples N  Area light samples (default: 8)\n"
           "  --threads N        Render workers (default: -1; -1 = auto)\n"
           "  --ray-depth N      Maximum specular recursion depth (default: 8)\n"
           "  --transition-width W  Smooth transition width in world X, 0 < W <= 3 (default: 0.24)\n"
           "  --wave-amplitude A    Main wave amplitude, 0 <= A <= 1.2 (default: 0.42)\n"
           "  --help             Show this help without rendering\n"
           "Sample counts must be integers from 1 to INT_MAX.\n"
           "Options accept --name N or --name=N; output may precede or follow them.\n",
           program);
}

/* Parse before opening output files or creating library objects. */
static int parse_options(int argc, char **argv, RenderOptions *options)
{
    int i, output_set = 0, positional_only = 0;
    for(i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if(!positional_only && strcmp(arg, "--help") == 0)
        { usage(argv[0]); return 0; }
        if(!positional_only && strcmp(arg, "--") == 0)
        { positional_only = 1; continue; }
        if(!positional_only && arg[0] == '-')
        {
            const char *equals = strchr(arg, '=');
            size_t length = equals ? (size_t)(equals - arg) : strlen(arg);
            const char *value;
            const char *digit;
            char *end;
            long number;
            int *destination = NULL;
            double *real_destination = NULL;
            if(length == strlen("--transition-width") && strncmp(arg, "--transition-width", length) == 0)
                real_destination = &options->transition_width;
            else if(length == strlen("--wave-amplitude") && strncmp(arg, "--wave-amplitude", length) == 0)
                real_destination = &options->wave_amplitude;
            if(real_destination)
            {
                double real_number;
                if(!equals && i + 1 >= argc)
                { fprintf(stderr, "Missing value for %s\n", arg); return -1; }
                value = equals ? equals + 1 : argv[++i];
                errno = 0;
                real_number = strtod(value, &end);
                if(errno == ERANGE || end == value || *end || real_number != real_number ||
                   (real_destination == &options->transition_width && (real_number <= 0 || real_number > 3)) ||
                   (real_destination == &options->wave_amplitude && (real_number < 0 || real_number > 1.2)))
                { fprintf(stderr, "Invalid mask parameter for %s: %s\n", arg, value); return -1; }
                *real_destination = real_number;
                continue;
            }
            if(length == strlen("--aa-samples") && strncmp(arg, "--aa-samples", length) == 0)
                destination = &options->aa_samples;
            else if(length == strlen("--path-samples") && strncmp(arg, "--path-samples", length) == 0)
                destination = &options->path_samples;
            else if(length == strlen("--light-samples") && strncmp(arg, "--light-samples", length) == 0)
                destination = &options->light_samples;
            else if(length == strlen("--threads") && strncmp(arg, "--threads", length) == 0)
                destination = &options->threads;
            else if(length == strlen("--ray-depth") && strncmp(arg, "--ray-depth", length) == 0)
                destination = &options->ray_depth;
            if(!destination)
            { fprintf(stderr, "Unknown option: %s\n", arg); return -1; }
            if(!equals && i + 1 >= argc)
            { fprintf(stderr, "Missing value for %s\n", arg); return -1; }
            value = equals ? equals + 1 : argv[++i];
            digit = value;
            if(destination == &options->threads && strcmp(value, "-1") == 0) ++digit;
            if(!*digit)
            { fprintf(stderr, "Invalid integer for %s: %s\n", arg, value); return -1; }
            for(; *digit; ++digit)
                if(*digit < '0' || *digit > '9')
                { fprintf(stderr, "Invalid integer for %s: %s\n", arg, value); return -1; }
            errno = 0;
            number = strtol(value, &end, 10);
            if(errno == ERANGE || *end || number > INT_MAX ||
               (number < 1 && !(destination == &options->threads && number == -1)))
            { fprintf(stderr, "Out-of-range value for %s: %s\n", arg, value); return -1; }
            if(destination == &options->ray_depth && number > 128)
            { fputs("Ray depth must be at most 128.\n", stderr); return -1; }
            *destination = (int)number;
        }
        else
        {
            if(output_set)
            { fprintf(stderr, "Unexpected argument: %s\n", arg); return -1; }
            options->output = arg;
            output_set = 1;
        }
    }
    return 1;
}

/* Result flags can contain warnings as well as errors. */
static int succeeded(yafaray_ResultFlags result)
{
    const int errors = YAFARAY_RESULT_ERROR_TYPE_UNKNOWN_PARAM |
        YAFARAY_RESULT_ERROR_WRONG_PARAM_TYPE | YAFARAY_RESULT_ERROR_ALREADY_EXISTS |
        YAFARAY_RESULT_ERROR_WHILE_CREATING | YAFARAY_RESULT_ERROR_NOT_FOUND |
        YAFARAY_RESULT_ERROR_DUPLICATED_NAME;
    return (result & errors) == 0;
}

static int material(yafaray_Scene *scene, yafaray_ParamMap *params,
                    yafaray_ParamMapList *nodes, const char *name,
                    double r, double g, double b, size_t *id)
{
    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "shinydiffusemat");
    yafaray_setParamMapColor(params, "color", r, g, b, 1.0);
    yafaray_setParamMapFloat(params, "diffuse_reflect", 1.0);
    yafaray_setParamMapFloat(params, "specular_reflect", 0.0);
    return succeeded(yafaray_createMaterial(scene, id, name, params, nodes));
}

static int checker_material(yafaray_Scene *scene, yafaray_ParamMap *params,
                            yafaray_ParamMapList *nodes, size_t *id)
{
    size_t image_id;
    int x, y;
    /* Generate a 64 x 64, 4 x 4 checkerboard in RAM. Colors are linear RGB. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "Color");
    yafaray_setParamMapString(params, "image_optimization", "none");
    yafaray_setParamMapInt(params, "width", 64);
    yafaray_setParamMapInt(params, "height", 64);
    if(!succeeded(yafaray_createImage(scene, "checker_image", &image_id, params))) return 0;
    for(y = 0; y < 64; ++y)
        for(x = 0; x < 64; ++x)
        {
            int red = ((x / 16 + y / 16) % 2) == 0;
            if(!yafaray_setImageColor(scene, image_id, x, y,
                    red ? 0.055f : 0.65f, red ? 0.11f : 0.68f,
                    red ? 0.16f : 0.62f, 1.0f)) return 0;
        }

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "image");
    yafaray_setParamMapString(params, "image_name", "checker_image");
    yafaray_setParamMapString(params, "interpolate", "bilinear");
    if(!succeeded(yafaray_createTexture(scene, "checker_texture", params))) return 0;

    /* World-space cube projection maps the horizontal floor without UV assets. */
    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "name", "checker_mapper");
    yafaray_setParamMapString(params, "type", "texture_mapper");
    yafaray_setParamMapString(params, "texture", "checker_texture");
    yafaray_setParamMapString(params, "texco", "global");
    yafaray_setParamMapString(params, "mapping", "cube");
    yafaray_addParamMapToList(nodes, params);

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "shinydiffusemat");
    yafaray_setParamMapString(params, "diffuse_shader", "checker_mapper");
    return succeeded(yafaray_createMaterial(scene, id, "checker", params, nodes));
}

static int mesh(yafaray_Scene *scene, yafaray_ParamMap *params, const char *name,
                const double vertices[][3], size_t vertex_count,
                const size_t faces[][3], size_t face_count, size_t material_id)
{
    size_t object_id, i;
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "mesh");
    if(!succeeded(yafaray_createObject(scene, &object_id, name, params))) return 0;
    for(i = 0; i < vertex_count; ++i)
        if(yafaray_addVertex(scene, object_id, vertices[i][0], vertices[i][1],
                            vertices[i][2]) != i) return 0;
    for(i = 0; i < face_count; ++i)
        if(!yafaray_addTriangle(scene, object_id, faces[i][0], faces[i][1],
                               faces[i][2], material_id)) return 0;
    return yafaray_initObject(scene, object_id, material_id);
}

static int blended_sphere(yafaray_Scene *scene, yafaray_ParamMap *params,
                          yafaray_ParamMapList *nodes, const RenderOptions *options)
{
    size_t matte_id, mirror_id, blend_id, image_id, object_id;
    int x, y;
    if(!material(scene, params, nodes, "matte_paint", 0.65, 0.045, 0.012, &matte_id)) return 0;
    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "mirror");
    yafaray_setParamMapColor(params, "color", 1, 1, 1, 1);
    yafaray_setParamMapFloat(params, "reflect", 0.95);
    if(!succeeded(yafaray_createMaterial(scene, &mirror_id, "polished_mirror", params, nodes))) return 0;

    /* A data mask, not an sRGB color image. World-space X/Z projection avoids
     * sphere UV seams and gives a wavy vertical boundary on the visible side. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "Color");
    yafaray_setParamMapString(params, "image_optimization", "none");
    yafaray_setParamMapString(params, "color_space", "LinearRGB");
    yafaray_setParamMapInt(params, "width", 512);
    yafaray_setParamMapInt(params, "height", 512);
    if(!succeeded(yafaray_createImage(scene, "blend_mask_image", &image_id, params))) return 0;
    for(y = 0; y < 512; ++y)
        for(x = 0; x < 512; ++x)
        {
            double world_x = -1.5 + 3.0 * (x + 0.5) / 512.0;
            double world_z = 2.7 - 3.0 * (y + 0.5) / 512.0;
            double t = world_z - 1.2;
            double boundary = options->wave_amplitude * (sin(4.8 * t) + (2.0/7.0) * sin(9.0 * t + 0.6));
            double weight = 0.5 + (world_x - boundary) / options->transition_width;
            if(weight < 0) weight = 0;
            if(weight > 1) weight = 1;
            weight = weight * weight * (3.0 - 2.0 * weight);
            if(!yafaray_setImageColor(scene, image_id, x, y,
                    (float)weight, (float)weight, (float)weight, 1)) return 0;
        }
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "image");
    yafaray_setParamMapString(params, "image_name", "blend_mask_image");
    yafaray_setParamMapString(params, "interpolate", "bilinear");
    if(!succeeded(yafaray_createTexture(scene, "blend_mask", params))) return 0;

    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "name", "mask_mapper");
    yafaray_setParamMapString(params, "type", "texture_mapper");
    yafaray_setParamMapString(params, "texture", "blend_mask");
    yafaray_setParamMapString(params, "texco", "global");
    yafaray_setParamMapString(params, "mapping", "plain");
    yafaray_setParamMapBool(params, "do_scalar", YAFARAY_BOOL_TRUE);
    yafaray_setParamMapInt(params, "proj_x", 1);
    yafaray_setParamMapInt(params, "proj_y", 3);
    yafaray_setParamMapInt(params, "proj_z", 2);
    yafaray_setParamMapVector(params, "scale", 2.0/3.0, 2.0/3.0, 1);
    yafaray_setParamMapVector(params, "offset", 0, -0.4, 0);
    yafaray_addParamMapToList(nodes, params);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "blend_mat");
    yafaray_setParamMapString(params, "material1", "matte_paint");
    yafaray_setParamMapString(params, "material2", "polished_mirror");
    yafaray_setParamMapString(params, "blend_shader", "mask_mapper");
    yafaray_setParamMapFloat(params, "blend_value", 0.5);
    if(!succeeded(yafaray_createMaterial(scene, &blend_id, "wavy_blend", params, nodes))) return 0;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "sphere");
    yafaray_setParamMapString(params, "material", "wavy_blend");
    yafaray_setParamMapVector(params, "center", 0, 0, 1.2);
    yafaray_setParamMapFloat(params, "radius", 1.2);
    return succeeded(yafaray_createObject(scene, &object_id, "blend_sphere", params));
}

int main(int argc, char **argv)
{
    const double floor_vertices[][3] = {
        {-200, -200, -0.01}, {200, -200, -0.01},
        {200, 200, -0.01}, {-200, 200, -0.01}
    };
    const size_t floor_faces[][3] = {{0, 1, 2}, {0, 2, 3}};
    const double emitter_vertices[][3] = {
        {-2, -1.5, 5}, {-2, 1.5, 5},
        {2, 1.5, 5}, {2, -1.5, 5}
    };
    RenderOptions options = {"material_blend.png", 32, 8, 8, -1, 8, 0.24, 0.42};
    const char *output;
    yafaray_Logger *logger = NULL;
    yafaray_ParamMap *params = NULL;
    yafaray_ParamMapList *nodes = NULL;
    yafaray_Scene *scene = NULL;
    yafaray_SurfaceIntegrator *integrator = NULL;
    yafaray_Film *film = NULL;
    yafaray_RenderControl *control = NULL;
    yafaray_RenderMonitor *monitor = NULL;
    size_t floor_material, emitter_material;
    int status = EXIT_FAILURE;
    FILE *file;
    long image_size;
    unsigned char header[33], ending[12];
    const char *extension;
    int png_output;
    size_t output_length;
    int parse_result = parse_options(argc, argv, &options);

    if(parse_result <= 0)
    {
        if(parse_result < 0) fputs("Use --help for usage.\n", stderr);
        return parse_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    output = options.output;
    output_length = strlen(output);
    if(output_length < 4)
    {
        fputs("Output filename must end with .tga or .png.\n", stderr);
        return EXIT_FAILURE;
    }
    /* libYafaRay selects its writer using a case-sensitive extension. */
    extension = output + output_length - 4;
    png_output = strcmp(extension, ".png") == 0;
    if(!png_output && strcmp(extension, ".tga") != 0)
    {
        fputs("Output filename must end with lowercase .tga or .png.\n", stderr);
        return EXIT_FAILURE;
    }
    if(yafaray_getVersionMajor() != YAFARAY_C_API_VERSION_MAJOR)
    {
        fputs("Incompatible libYafaRay API version.\n", stderr);
        return EXIT_FAILURE;
    }
    printf("Render settings: AA_minsamples=%d path_samples=%d samples=%d threads=%d raydepth=%d\n",
           options.aa_samples, options.path_samples, options.light_samples, options.threads, options.ray_depth);
    printf("Mask settings: transition_width=%g wave_amplitude=%g\n",
           options.transition_width, options.wave_amplitude);

    logger = yafaray_createLogger("material_blend", NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);
    if(!logger) goto cleanup;
    yafaray_setConsoleVerbosityLevel(logger, YAFARAY_LOG_LEVEL_INFO);
    params = yafaray_createParamMap();
    nodes = yafaray_createParamMapList();
    scene = yafaray_createScene(logger, "wavy_material_blend");
    control = yafaray_createRenderControl();
    monitor = yafaray_createRenderMonitor(NULL, NULL, YAFARAY_DISPLAY_CONSOLE_HIDDEN);
    if(!params || !nodes || !scene || !control || !monitor) goto cleanup;

    if(!checker_material(scene, params, nodes, &floor_material) ||
       !material(scene, params, nodes, "emitter_surface", 0.8, 0.8, 0.8, &emitter_material)) goto cleanup;
    if(!mesh(scene, params, "floor", floor_vertices, 4, floor_faces, 2, floor_material) ||
       !mesh(scene, params, "emitter", emitter_vertices, 4, floor_faces, 2, emitter_material) ||
       !blended_sphere(scene, params, nodes, &options)) goto cleanup;

    yafaray_clearParamMap(params);
    /* A 4 x 3 area emitter. Edge ordering points its emitting side downward. */
    yafaray_setParamMapString(params, "type", "arealight");
    yafaray_setParamMapVector(params, "corner", -2.0, -1.5, 5.0);
    yafaray_setParamMapVector(params, "point1", -2.0, 1.5, 5.0);
    yafaray_setParamMapVector(params, "point2", 2.0, -1.5, 5.0);
    yafaray_setParamMapColor(params, "color", 1.0, 0.95, 0.85, 1.0);
    yafaray_setParamMapFloat(params, "power", 8.0);
    yafaray_setParamMapInt(params, "samples", options.light_samples);
    yafaray_setParamMapString(params, "object_name", "emitter");
    if(!succeeded(yafaray_createLight(scene, "key", params))) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "constant");
    yafaray_setParamMapColor(params, "color", 0.16, 0.20, 0.28, 1.0);
    if(!succeeded(yafaray_defineBackground(scene, params))) goto cleanup;

    /* Diffuse fill and long specular chains serve different purposes. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "pathtracing");
    yafaray_setParamMapInt(params, "path_samples", options.path_samples);
    yafaray_setParamMapInt(params, "bounces", 3);
    yafaray_setParamMapInt(params, "russian_roulette_min_bounces", 3);
    yafaray_setParamMapInt(params, "raydepth", options.ray_depth);
    yafaray_setParamMapBool(params, "transpShad", YAFARAY_BOOL_TRUE);
    yafaray_setParamMapInt(params, "shadowDepth", 8);
    yafaray_setParamMapInt(params, "threads", options.threads);
    integrator = yafaray_createSurfaceIntegrator(logger, "path", params);
    if(!integrator) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapInt(params, "width", WIDTH);
    yafaray_setParamMapInt(params, "height", HEIGHT);
    yafaray_setParamMapInt(params, "AA_passes", 1);
    yafaray_setParamMapInt(params, "AA_minsamples", options.aa_samples);
    film = yafaray_createFilm(logger, integrator, "film", params);
    if(!film) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "perspective");
    yafaray_setParamMapInt(params, "resx", WIDTH);
    yafaray_setParamMapInt(params, "resy", HEIGHT);
    yafaray_setParamMapFloat(params, "focal", 1.35);
    yafaray_setParamMapVector(params, "from", 0.2, -6.5, 2.8);
    yafaray_setParamMapVector(params, "to", 0, 0, 1.2);
    /* 'up' is a point, not a direction vector. */
    yafaray_setParamMapVector(params, "up", 0.2, -6.5, 3.8);
    if(!succeeded(yafaray_defineCamera(film, params))) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "image_path", output);
    yafaray_setParamMapBool(params, "alpha_channel", YAFARAY_BOOL_FALSE);
    if(!succeeded(yafaray_createOutput(film, "image", params))) goto cleanup;

    yafaray_setRenderControlForNormalStart(control);
    if(!yafaray_preprocessScene(scene, control,
            yafaray_checkAndClearSceneModifiedFlags(scene))) goto cleanup;
    yafaray_preprocessSurfaceIntegrator(monitor, integrator, control, scene);

    /* Check the destination and discard any old result before rendering, so a
     * previous image cannot make a failed render look successful. */
    file = fopen(output, "wb");
    if(!file) { perror(output); goto cleanup; }
    if(fclose(file) != 0) goto cleanup;
    yafaray_render(control, monitor, integrator, film);

    /* The render API returns void; confirm the output was actually written. */
    file = fopen(output, "rb");
    if(!file) goto cleanup;
    if(fread(header, 1, sizeof(header), file) != sizeof(header))
    { fclose(file); goto cleanup; }
    if(fseek(file, 0, SEEK_END) != 0) { fclose(file); goto cleanup; }
    image_size = ftell(file);
    if(png_output)
    {
        if(image_size < 45 || fseek(file, -12L, SEEK_END) != 0 ||
           fread(ending, 1, sizeof(ending), file) != sizeof(ending))
        { fclose(file); goto cleanup; }
    }
    fclose(file);
    if(png_output)
    {
        /* Signature, IHDR size/type/dimensions and terminal IEND chunk.
         * libYafaRay/libpng writes the PNG directly; no TGA conversion. */
        if(memcmp(header, "\211PNG\r\n\032\n", 8) != 0 ||
           memcmp(header + 8, "\0\0\0\015IHDR", 8) != 0 ||
           header[16] != 0 || header[17] != 0 ||
           header[18] * 256 + header[19] != WIDTH ||
           header[20] != 0 || header[21] != 0 ||
           header[22] * 256 + header[23] != HEIGHT ||
           memcmp(ending, "\0\0\0\0IEND\256\102\140\202", 12) != 0) goto cleanup;
    }
    else if(header[1] != 0 || header[2] != 2 ||
       header[12] + 256 * header[13] != WIDTH ||
       header[14] + 256 * header[15] != HEIGHT ||
       (header[16] != 24 && header[16] != 32) ||
       image_size < 18L + header[0] + (long)WIDTH * HEIGHT * (header[16] / 8)) goto cleanup;
    printf("Saved %s (%d x %d %s).\n", output, WIDTH, HEIGHT, png_output ? "PNG" : "TGA");
    status = EXIT_SUCCESS;

cleanup:
    if(status != EXIT_SUCCESS)
    {
        fputs("Scene rendering or image output failed.\n", stderr);
        if(png_output) fputs("PNG requires libYafaRay built with YAFARAY_WITH_PNG=ON.\n", stderr);
    }
    if(monitor) yafaray_destroyRenderMonitor(monitor);
    if(control) yafaray_destroyRenderControl(control);
    if(film) yafaray_destroyFilm(film);
    if(integrator) yafaray_destroySurfaceIntegrator(integrator);
    if(scene) yafaray_destroyScene(scene);
    if(nodes) yafaray_destroyParamMapList(nodes);
    if(params) yafaray_destroyParamMap(params);
    if(logger) yafaray_destroyLogger(logger);
    return status;
}


