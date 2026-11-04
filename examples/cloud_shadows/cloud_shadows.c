/* Simple C90 client: a JPEG cloud sheet casting transparent shadows on a checkerboard, rendered by the installed DLL.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
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
    int cloud_shadows;
    double mask_gamma;
} RenderOptions;

static void usage(const char *program)
{
    printf("Usage: %s [output.tga|output.png] [options]\n"
           "  --aa-samples N     Film AA_minsamples (default: 16)\n"
           "  --path-samples N   Integrator path_samples (default: 4)\n"
           "  --light-samples N  Sunlight samples (default: 8)\n"
           "  --threads N        Render workers (default: -1; -1 = auto)\n"
           "  --ray-depth N      Maximum specular recursion depth (default: 8)\n"
           "  --no-cloud-shadows Disable sun shadows for a control render\n"
           "  --mask-gamma G     Mask gamma: 0=opaque, 1=original, >1=brighter (default: 1)\n"
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
        if(!positional_only && strcmp(arg, "--no-cloud-shadows") == 0)
        { options->cloud_shadows = 0; continue; }
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
            if(length == strlen("--mask-gamma") && strncmp(arg, "--mask-gamma", length) == 0)
            {
                double gamma;
                if(!equals && i + 1 >= argc)
                { fprintf(stderr, "Missing value for %s\n", arg); return -1; }
                value = equals ? equals + 1 : argv[++i];
                errno = 0;
                gamma = strtod(value, &end);
                if(errno == ERANGE || end == value || *end || gamma != gamma || gamma < 0 || gamma > DBL_MAX)
                { fprintf(stderr, "Invalid mask gamma: %s\n", value); return -1; }
                options->mask_gamma = gamma;
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

static int checker_material(yafaray_Scene *scene, yafaray_ParamMap *params,
                            yafaray_ParamMapList *nodes, size_t *id)
{
    size_t image_id;
    int x, y;
    /* Generate a 64 x 64, 2 x 2 checkerboard in RAM. Colors are linear RGB. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "Color");
    yafaray_setParamMapString(params, "image_optimization", "none");
    yafaray_setParamMapInt(params, "width", 64);
    yafaray_setParamMapInt(params, "height", 64);
    if(!succeeded(yafaray_createImage(scene, "checker_image", &image_id, params))) return 0;
    for(y = 0; y < 64; ++y)
        for(x = 0; x < 64; ++x)
        {
            int red = ((x / 32 + y / 32) % 2) == 0;
            if(!yafaray_setImageColor(scene, image_id, x, y,
                    red ? 0.10f : 0.58f, red ? 0.12f : 0.60f,
                    red ? 0.13f : 0.55f, 1.0f)) return 0;
        }

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "image");
    yafaray_setParamMapString(params, "image_name", "checker_image");
    yafaray_setParamMapString(params, "interpolate", "bilinear");
    if(!succeeded(yafaray_createTexture(scene, "checker_texture", params))) return 0;

    /* World-space planar projection: a 10-unit repeat, five-unit cells. */
    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "name", "checker_mapper");
    yafaray_setParamMapString(params, "type", "texture_mapper");
    yafaray_setParamMapString(params, "texture", "checker_texture");
    yafaray_setParamMapString(params, "texco", "global");
    yafaray_setParamMapString(params, "mapping", "plain");
    yafaray_setParamMapVector(params, "scale", 0.2, 0.2, 1);
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

static float mask_value(float value, double gamma)
{
    if(gamma == 0) return 0;
    if(gamma == 1) return value;
    if(value <= 0) return 0;
    if(value >= 1) return 1;
    return (float)pow((double)value, 1.0 / gamma);
}

static int cloud_material(yafaray_Scene *scene, yafaray_ParamMap *params,
                          yafaray_ParamMapList *nodes, size_t *id, double mask_gamma)
{
    const char *names[] = {"cloud_color", "cloud_transparency"};
    const char *files[] = {"../textures/turquoise-sky-pbr-texture_color.jpeg",
                           "../textures/turquoise-sky-pbr-texture_transparency.jpeg"};
    int i;
    yafaray_clearParamMapList(nodes);
    for(i = 0; i < 2; ++i)
    {
        size_t image_id;
        FILE *input = fopen(files[i], "rb");
        if(!input) { perror(files[i]); return 0; }
        fclose(input);
        yafaray_clearParamMap(params);
        yafaray_setParamMapString(params, "filename", files[i]);
        yafaray_setParamMapString(params, "color_space", i ? "LinearRGB" : "sRGB");
        yafaray_setParamMapString(params, "image_optimization", "none");
        if(!succeeded(yafaray_createImage(scene, names[i], &image_id, params)) ||
           yafaray_getImageWidth(scene, image_id) <= 1 ||
           yafaray_getImageHeight(scene, image_id) <= 1)
        { fputs("JPEG load failed; enable YAFARAY_WITH_JPEG.\n", stderr); return 0; }
        if(i == 1)
        {
            int x, y, width = yafaray_getImageWidth(scene, image_id);
            int height = yafaray_getImageHeight(scene, image_id);
            double mean = 0;
            for(y = 0; y < height; ++y)
                for(x = 0; x < width; ++x)
                {
                    float r, g, b, a;
                    if(!yafaray_getImageColor(scene, image_id, x, y, &r, &g, &b, &a)) return 0;
                    r = mask_value(r, mask_gamma);
                    g = mask_value(g, mask_gamma);
                    b = mask_value(b, mask_gamma);
                    mean += ((double)r + g + b) / 3.0;
                    /* Preserve the original buffer exactly at gamma=1. */
                    if(mask_gamma != 1 && !yafaray_setImageColor(scene, image_id, x, y, r, g, b, a)) return 0;
                }
            printf("Mask gamma=%g mean transparency=%.6f\n", mask_gamma, mean / ((double)width * height));
        }
        yafaray_clearParamMap(params);
        yafaray_setParamMapString(params, "type", "image");
        yafaray_setParamMapString(params, "image_name", names[i]);
        yafaray_setParamMapString(params, "interpolate", "bilinear");
        if(!succeeded(yafaray_createTexture(scene, names[i], params))) return 0;
        yafaray_clearParamMap(params);
        yafaray_setParamMapString(params, "name", names[i]);
        yafaray_setParamMapString(params, "type", "texture_mapper");
        yafaray_setParamMapString(params, "texture", names[i]);
        yafaray_setParamMapString(params, "texco", "global");
        yafaray_setParamMapString(params, "mapping", "plain");
        yafaray_setParamMapVector(params, "scale", 1.0/12.0, 1.0/12.0, 1);
        yafaray_setParamMapBool(params, "do_scalar", YAFARAY_BOOL_TRUE);
        yafaray_addParamMapToList(nodes, params);
    }
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "shinydiffusemat");
    yafaray_setParamMapString(params, "diffuse_shader", "cloud_color");
    yafaray_setParamMapString(params, "transparency_shader", "cloud_transparency");
    yafaray_setParamMapFloat(params, "diffuse_reflect", 1.0);
    yafaray_setParamMapFloat(params, "transmit_filter", 0.0);
    return succeeded(yafaray_createMaterial(scene, id, "clouds", params, nodes));
}

static int fill_light(yafaray_Scene *scene, yafaray_ParamMap *params,
                      const char *name, double z, double power)
{
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "directional");
    yafaray_setParamMapVector(params, "direction", 0, 0, z);
    yafaray_setParamMapColor(params, "color", 1, 1, 1, 1);
    yafaray_setParamMapFloat(params, "power", power);
    yafaray_setParamMapBool(params, "infinite", YAFARAY_BOOL_TRUE);
    yafaray_setParamMapBool(params, "cast_shadows", YAFARAY_BOOL_FALSE);
    return succeeded(yafaray_createLight(scene, name, params));
}

int main(int argc, char **argv)
{
    /* A downward-facing sheet above the camera; the ground faces upward. */
    const double cloud_vertices[][3] = {
        {-200,-200,6}, {-200,200,6}, {200,200,6}, {200,-200,6}
    };
    const double floor_vertices[][3] = {
        {-200, -200, -0.01}, {200, -200, -0.01},
        {200, 200, -0.01}, {-200, 200, -0.01}
    };
    const size_t floor_faces[][3] = {{0, 1, 2}, {0, 2, 3}};
    RenderOptions options = {"cloud_shadows.png", 16, 4, 8, -1, 8, 1, 1.0};
    const char *output;
    yafaray_Logger *logger = NULL;
    yafaray_ParamMap *params = NULL;
    yafaray_ParamMapList *nodes = NULL;
    yafaray_Scene *scene = NULL;
    yafaray_SurfaceIntegrator *integrator = NULL;
    yafaray_Film *film = NULL;
    yafaray_RenderControl *control = NULL;
    yafaray_RenderMonitor *monitor = NULL;
    size_t floor_material, clouds_material;
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
    printf("Sun shadows: %s\n", options.cloud_shadows ? "enabled" : "disabled");

    logger = yafaray_createLogger("cloud_shadows", NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);
    if(!logger) goto cleanup;
    yafaray_setConsoleVerbosityLevel(logger, YAFARAY_LOG_LEVEL_INFO);
    params = yafaray_createParamMap();
    nodes = yafaray_createParamMapList();
    scene = yafaray_createScene(logger, "ground_and_clouds");
    control = yafaray_createRenderControl();
    monitor = yafaray_createRenderMonitor(NULL, NULL, YAFARAY_DISPLAY_CONSOLE_HIDDEN);
    if(!params || !nodes || !scene || !control || !monitor) goto cleanup;

    if(!checker_material(scene, params, nodes, &floor_material) ||
       !cloud_material(scene, params, nodes, &clouds_material, options.mask_gamma) ||
       !mesh(scene, params, "ground", floor_vertices, 4, floor_faces, 2, floor_material) ||
       !mesh(scene, params, "cloud_sheet", cloud_vertices, 4, floor_faces, 2, clouds_material)) goto cleanup;

    /* Direction points toward the sun; its small angular size softens shadows. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "sunlight");
    yafaray_setParamMapVector(params, "direction", 0.45, -0.30, 1);
    yafaray_setParamMapColor(params, "color", 1.0, 0.97, 0.90, 1);
    yafaray_setParamMapFloat(params, "power", 1.4);
    yafaray_setParamMapFloat(params, "angle", 0.27);
    yafaray_setParamMapInt(params, "samples", options.light_samples);
    yafaray_setParamMapBool(params, "cast_shadows", options.cloud_shadows ? YAFARAY_BOOL_TRUE : YAFARAY_BOOL_FALSE);
    if(!succeeded(yafaray_createLight(scene, "sun", params)) ||
       !fill_light(scene, params, "sky_fill", 1, 0.15) ||
       !fill_light(scene, params, "underside_fill", -1, 0.75)) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "constant");
    yafaray_setParamMapColor(params, "color", 0.08, 0.55, 0.60, 1.0);
    if(!succeeded(yafaray_defineBackground(scene, params))) goto cleanup;

    /* Transparent camera rays and filtered sun shadows cross the cloud sheet. */
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
    yafaray_setParamMapFloat(params, "focal", 0.80);
    yafaray_setParamMapVector(params, "from", 0, -8, 3);
    yafaray_setParamMapVector(params, "to", 0, 12, 3);
    /* 'up' is a point, not a direction vector. */
    yafaray_setParamMapVector(params, "up", 0, -8, 4);
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


