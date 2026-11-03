/* Simple C90 client: a textured cube on a floor, rendered by the installed DLL.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <yafaray_c_api.h>

#define WIDTH 480
#define HEIGHT 360

typedef struct RenderOptions
{
    const char *output;
    int aa_samples;
    int path_samples;
    int light_samples;
    int threads;
} RenderOptions;

static void usage(const char *program)
{
    printf("Usage: %s [output.tga] [options]\n"
           "  --aa-samples N     Film AA_minsamples (default: 32)\n"
           "  --path-samples N   Integrator path_samples (default: 8)\n"
           "  --light-samples N  Area light samples (default: 8)\n"
           "  --threads N        Render workers (default: 2; -1 = auto)\n"
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
            if(length == strlen("--aa-samples") && strncmp(arg, "--aa-samples", length) == 0)
                destination = &options->aa_samples;
            else if(length == strlen("--path-samples") && strncmp(arg, "--path-samples", length) == 0)
                destination = &options->path_samples;
            else if(length == strlen("--light-samples") && strncmp(arg, "--light-samples", length) == 0)
                destination = &options->light_samples;
            else if(length == strlen("--threads") && strncmp(arg, "--threads", length) == 0)
                destination = &options->threads;
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
    return succeeded(yafaray_createMaterial(scene, id, name, params, nodes));
}

static int checker_material(yafaray_Scene *scene, yafaray_ParamMap *params,
                            yafaray_ParamMapList *nodes, size_t *id)
{
    size_t image_id;
    int x, y;
    /* Generate a 64 x 64, 8 x 8 checkerboard in RAM. Colors are linear RGB. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "Color");
    yafaray_setParamMapString(params, "image_optimization", "none");
    yafaray_setParamMapInt(params, "width", 64);
    yafaray_setParamMapInt(params, "height", 64);
    if(!succeeded(yafaray_createImage(scene, "checker_image", &image_id, params))) return 0;
    for(y = 0; y < 64; ++y)
        for(x = 0; x < 64; ++x)
        {
            int red = ((x / 8 + y / 8) % 2) == 0;
            if(!yafaray_setImageColor(scene, image_id, x, y,
                    red ? 0.75f : 0.85f, red ? 0.08f : 0.78f,
                    red ? 0.035f : 0.62f, 1.0f)) return 0;
        }

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "image");
    yafaray_setParamMapString(params, "image_name", "checker_image");
    yafaray_setParamMapString(params, "interpolate", "bilinear");
    if(!succeeded(yafaray_createTexture(scene, "checker_texture", params))) return 0;

    /* Cube projection uses the face normal; no UVs or external assets needed. */
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

static int mirror_sphere(yafaray_Scene *scene, yafaray_ParamMap *params,
                         yafaray_ParamMapList *nodes)
{
    size_t material_id, object_id;
    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "mirror");
    yafaray_setParamMapColor(params, "color", 1.0, 1.0, 1.0, 1.0);
    yafaray_setParamMapFloat(params, "reflect", 0.95);
    if(!succeeded(yafaray_createMaterial(scene, &material_id, "mirror", params, nodes))) return 0;

    /* Analytic sphere: no tessellation or mesh vertex data is necessary. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "sphere");
    yafaray_setParamMapString(params, "material", "mirror");
    yafaray_setParamMapVector(params, "center", 2.5, 0.0, 0.8);
    yafaray_setParamMapFloat(params, "radius", 0.8);
    return succeeded(yafaray_createObject(scene, &object_id, "mirror_sphere", params));
}

static int glass_sphere(yafaray_Scene *scene, yafaray_ParamMap *params,
                        yafaray_ParamMapList *nodes)
{
    size_t material_id, object_id;
    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "glass");
    yafaray_setParamMapFloat(params, "IOR", 1.5);
    yafaray_setParamMapColor(params, "filter_color", 1.0, 1.0, 1.0, 1.0);
    yafaray_setParamMapColor(params, "mirror_color", 1.0, 1.0, 1.0, 1.0);
    /* Approximate transmission for shadow rays; camera rays still refract. */
    yafaray_setParamMapBool(params, "fake_shadows", YAFARAY_BOOL_TRUE);
    if(!succeeded(yafaray_createMaterial(scene, &material_id, "glass", params, nodes))) return 0;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "sphere");
    yafaray_setParamMapString(params, "material", "glass");
    yafaray_setParamMapVector(params, "center", 1.6, -2.0, 0.8);
    yafaray_setParamMapFloat(params, "radius", 0.8);
    return succeeded(yafaray_createObject(scene, &object_id, "glass_sphere", params));
}

int main(int argc, char **argv)
{
    const double cube[][3] = {
        {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0},
        {-1, -1, 2}, {1, -1, 2}, {1, 1, 2}, {-1, 1, 2}
    };
    const size_t cube_faces[][3] = {
        {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5},
        {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}
    };
    const double floor_vertices[][3] = {
        {-200, -200, -0.01}, {200, -200, -0.01},
        {200, 200, -0.01}, {-200, 200, -0.01}
    };
    const size_t floor_faces[][3] = {{0, 1, 2}, {0, 2, 3}};
    const double emitter_vertices[][3] = {
        {-4.5, -5.5, 7}, {-4.5, -2.5, 7},
        {-1.5, -2.5, 7}, {-1.5, -5.5, 7}
    };
    RenderOptions options = {"scene.tga", 32, 8, 8, 2};
    const char *output;
    yafaray_Logger *logger = NULL;
    yafaray_ParamMap *params = NULL;
    yafaray_ParamMapList *nodes = NULL;
    yafaray_Scene *scene = NULL;
    yafaray_SurfaceIntegrator *integrator = NULL;
    yafaray_Film *film = NULL;
    yafaray_RenderControl *control = NULL;
    yafaray_RenderMonitor *monitor = NULL;
    size_t cube_material, floor_material;
    int status = EXIT_FAILURE;
    FILE *file;
    long image_size;
    unsigned char header[18];
    size_t output_length;
    int parse_result = parse_options(argc, argv, &options);

    if(parse_result <= 0)
    {
        if(parse_result < 0) fputs("Use --help for usage.\n", stderr);
        return parse_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    output = options.output;
    output_length = strlen(output);
    if(output_length < 4 ||
       (strcmp(output + output_length - 4, ".tga") != 0 &&
        strcmp(output + output_length - 4, ".TGA") != 0))
    {
        fputs("Output filename must end with .tga or .TGA.\n", stderr);
        return EXIT_FAILURE;
    }
    if(yafaray_getVersionMajor() != YAFARAY_C_API_VERSION_MAJOR)
    {
        fputs("Incompatible libYafaRay API version.\n", stderr);
        return EXIT_FAILURE;
    }
    printf("Render settings: AA_minsamples=%d path_samples=%d samples=%d threads=%d\n",
           options.aa_samples, options.path_samples, options.light_samples, options.threads);

    logger = yafaray_createLogger("render_scene", NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);
    if(!logger) goto cleanup;
    yafaray_setConsoleVerbosityLevel(logger, YAFARAY_LOG_LEVEL_INFO);
    params = yafaray_createParamMap();
    nodes = yafaray_createParamMapList();
    scene = yafaray_createScene(logger, "cube_on_floor");
    control = yafaray_createRenderControl();
    monitor = yafaray_createRenderMonitor(NULL, NULL, YAFARAY_DISPLAY_CONSOLE_HIDDEN);
    if(!params || !nodes || !scene || !control || !monitor) goto cleanup;

    /* Texture pixels are generated in memory; no external scene assets. */
    if(!material(scene, params, nodes, "floor", 0.55, 0.60, 0.65, &floor_material) ||
       !checker_material(scene, params, nodes, &cube_material)) goto cleanup;
    if(!mesh(scene, params, "cube", cube, 8, cube_faces, 12, cube_material) ||
       !mesh(scene, params, "floor", floor_vertices, 4, floor_faces, 2, floor_material) ||
       !mesh(scene, params, "emitter", emitter_vertices, 4, floor_faces, 2, floor_material)) goto cleanup;
    if(!mirror_sphere(scene, params, nodes)) goto cleanup;
    if(!glass_sphere(scene, params, nodes)) goto cleanup;

    yafaray_clearParamMap(params);
    /* A 3 x 3 area emitter. Edge ordering points its emitting side downward. */
    yafaray_setParamMapString(params, "type", "arealight");
    yafaray_setParamMapVector(params, "corner", -4.5, -5.5, 7.0);
    yafaray_setParamMapVector(params, "point1", -4.5, -2.5, 7.0);
    yafaray_setParamMapVector(params, "point2", -1.5, -5.5, 7.0);
    yafaray_setParamMapColor(params, "color", 1.0, 0.95, 0.85, 1.0);
    yafaray_setParamMapFloat(params, "power", 12.0);
    yafaray_setParamMapInt(params, "samples", options.light_samples);
    yafaray_setParamMapString(params, "object_name", "emitter");
    if(!succeeded(yafaray_createLight(scene, "key", params))) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "constant");
    yafaray_setParamMapColor(params, "color", 0.08, 0.10, 0.15, 1.0);
    if(!succeeded(yafaray_defineBackground(scene, params))) goto cleanup;

    /* Diffuse paths carry light reflected by the floor onto the cube. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "pathtracing");
    yafaray_setParamMapInt(params, "path_samples", options.path_samples);
    yafaray_setParamMapInt(params, "bounces", 3);
    yafaray_setParamMapInt(params, "russian_roulette_min_bounces", 3);
    yafaray_setParamMapInt(params, "raydepth", 8);
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
    yafaray_setParamMapFloat(params, "focal", 1.1);
    yafaray_setParamMapVector(params, "from", 5.0, -7.0, 4.5);
    yafaray_setParamMapVector(params, "to", 0.0, 0.0, 0.8);
    /* 'up' is a point, not a direction vector. */
    yafaray_setParamMapVector(params, "up", 5.0, -7.0, 5.5);
    if(!succeeded(yafaray_defineCamera(film, params))) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "image_path", output);
    yafaray_setParamMapBool(params, "alpha_channel", YAFARAY_BOOL_FALSE);
    if(!succeeded(yafaray_createOutput(film, "tga", params))) goto cleanup;

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
    fclose(file);
    if(header[1] != 0 || header[2] != 2 ||
       header[12] + 256 * header[13] != WIDTH ||
       header[14] + 256 * header[15] != HEIGHT ||
       (header[16] != 24 && header[16] != 32) ||
       image_size < 18L + header[0] + (long)WIDTH * HEIGHT * (header[16] / 8)) goto cleanup;
    printf("Saved %s (%d x %d TGA).\n", output, WIDTH, HEIGHT);
    status = EXIT_SUCCESS;

cleanup:
    if(status != EXIT_SUCCESS) fputs("Scene rendering or TGA output failed.\n", stderr);
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
