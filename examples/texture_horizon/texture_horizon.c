/* Periodic floor/ceiling textures under grazing angles. C90 external client.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <yafaray_c_api.h>

#define WIDTH 800
#define HEIGHT 500

static int ok(yafaray_ResultFlags result)
{
    return !(result & (YAFARAY_RESULT_ERROR_TYPE_UNKNOWN_PARAM |
        YAFARAY_RESULT_ERROR_WRONG_PARAM_TYPE | YAFARAY_RESULT_ERROR_ALREADY_EXISTS |
        YAFARAY_RESULT_ERROR_WHILE_CREATING | YAFARAY_RESULT_ERROR_NOT_FOUND |
        YAFARAY_RESULT_ERROR_DUPLICATED_NAME));
}

static int integer(const char *text, int threads, int *value)
{
    char *end;
    long number;
    const char *p = text;
    if(threads && strcmp(text, "-1") == 0) { *value = -1; return 1; }
    if(!*p) return 0;
    for(; *p; ++p) if(*p < '0' || *p > '9') return 0;
    errno = 0;
    number = strtol(text, &end, 10);
    if(errno || *end || number < 1 || number > INT_MAX) return 0;
    *value = (int)number;
    return 1;
}

static void usage(void)
{
    puts("Usage: yafaray_texture_horizon [output.tga] [options]\n"
         "  --filter none|bilinear|bicubic  Texture interpolation (default bilinear)\n"
         "  --aa-samples N                 Camera samples per pixel (default 16)\n"
         "  --threads N                    Workers (default -1 = auto)\n"
         "  --help                         Show help\n"
         "Output defaults to horizon.tga; existing output is overwritten.");
}

static int surface(yafaray_Scene *scene, yafaray_ParamMap *p,
                    yafaray_ParamMapList *nodes, const char *name,
                    const char *filter, double z, double period,
                    double r, double g, double b, int ceiling)
{
    size_t image, material, object, uv[4];
    int x, y, i;
    double vertices[4][3] = {
        {-10000, -10, 0}, {10000, -10, 0},
        {10000, 10000, 0}, {-10000, 10000, 0}
    };
    /* Two cells per axis: one full repeat is 'period' world units. */
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "Color");
    yafaray_setParamMapString(p, "image_optimization", "none");
    yafaray_setParamMapInt(p, "width", 64);
    yafaray_setParamMapInt(p, "height", 64);
    if(!ok(yafaray_createImage(scene, name, &image, p))) return 0;
    for(y = 0; y < 64; ++y)
        for(x = 0; x < 64; ++x)
        {
            double level = ((x / 32 + y / 32) % 2) ? 0.04 : 0.8;
            if(!yafaray_setImageColor(scene, image, x, y,
                    (float)(r * level), (float)(g * level), (float)(b * level), 1.0f)) return 0;
        }
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "image");
    yafaray_setParamMapString(p, "image_name", name);
    yafaray_setParamMapString(p, "interpolate", filter);
    if(!ok(yafaray_createTexture(scene, name, p))) return 0;

    yafaray_clearParamMapList(nodes);
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "texture_mapper");
    yafaray_setParamMapString(p, "name", "map");
    yafaray_setParamMapString(p, "texture", name);
    yafaray_setParamMapString(p, "texco", "uv");
    yafaray_setParamMapString(p, "mapping", "plain");
    yafaray_addParamMapToList(nodes, p);
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "shinydiffusemat");
    yafaray_setParamMapString(p, "diffuse_shader", "map");
    yafaray_setParamMapFloat(p, "emit", 1.0);
    if(!ok(yafaray_createMaterial(scene, &material, name, p, nodes))) return 0;

    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "mesh");
    yafaray_setParamMapBool(p, "has_uv", YAFARAY_BOOL_TRUE);
    if(!ok(yafaray_createObject(scene, &object, name, p))) return 0;
    for(i = 0; i < 4; ++i)
    {
        if(yafaray_addVertex(scene, object, vertices[i][0], vertices[i][1], z) != (size_t)i) return 0;
        uv[i] = yafaray_addUv(scene, object, vertices[i][0] / period, vertices[i][1] / period);
    }
    if(ceiling)
    {
        if(!yafaray_addTriangleWithUv(scene, object, 0, 2, 1, uv[0], uv[2], uv[1], material) ||
           !yafaray_addTriangleWithUv(scene, object, 0, 3, 2, uv[0], uv[3], uv[2], material)) return 0;
    }
    else
    {
        if(!yafaray_addTriangleWithUv(scene, object, 0, 1, 2, uv[0], uv[1], uv[2], material) ||
           !yafaray_addTriangleWithUv(scene, object, 0, 2, 3, uv[0], uv[2], uv[3], material)) return 0;
    }
    return yafaray_initObject(scene, object, material);
}

int main(int argc, char **argv)
{
    const char *output = "horizon.tga", *filter = "bilinear";
    int aa = 16, threads = -1, i, output_set = 0, status = EXIT_FAILURE;
    yafaray_Logger *logger = NULL;
    yafaray_ParamMap *p = NULL;
    yafaray_ParamMapList *nodes = NULL;
    yafaray_Scene *scene = NULL;
    yafaray_SurfaceIntegrator *integrator = NULL;
    yafaray_Film *film = NULL;
    yafaray_RenderControl *control = NULL;
    yafaray_RenderMonitor *monitor = NULL;
    FILE *file;
    unsigned char header[18];
    long size;
    for(i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if(strcmp(arg, "--help") == 0) { usage(); return EXIT_SUCCESS; }
        if(strcmp(arg, "--filter") == 0 || strcmp(arg, "--aa-samples") == 0 || strcmp(arg, "--threads") == 0)
        {
            if(++i == argc) { fprintf(stderr, "Missing value: %s\n", arg); return EXIT_FAILURE; }
            if(strcmp(arg, "--filter") == 0) filter = argv[i];
            else if(!integer(argv[i], strcmp(arg, "--threads") == 0,
                    strcmp(arg, "--threads") == 0 ? &threads : &aa))
            { fputs("Invalid positive integer (threads also accepts -1).\n", stderr); return EXIT_FAILURE; }
        }
        else if(arg[0] == '-' || output_set)
        { fprintf(stderr, "Unexpected argument: %s\n", arg); return EXIT_FAILURE; }
        else { output = arg; output_set = 1; }
    }
    if(strcmp(filter, "none") && strcmp(filter, "bilinear") && strcmp(filter, "bicubic"))
    { fputs("Filter must be none, bilinear or bicubic.\n", stderr); return EXIT_FAILURE; }
    if(strlen(output) < 4 || strcmp(output + strlen(output) - 4, ".tga"))
    { fputs("Output must end in .tga.\n", stderr); return EXIT_FAILURE; }
    if(yafaray_getVersionMajor() != YAFARAY_C_API_VERSION_MAJOR) return EXIT_FAILURE;
    printf("Filter=%s AA_minsamples=%d threads=%d; floor period=2, ceiling period=3.4\n", filter, aa, threads);
    logger = yafaray_createLogger("texture_horizon", NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);
    if(!logger) goto cleanup;
    yafaray_setConsoleVerbosityLevel(logger, YAFARAY_LOG_LEVEL_INFO);
    p = yafaray_createParamMap();
    nodes = yafaray_createParamMapList();
    scene = yafaray_createScene(logger, "floor_and_ceiling");
    control = yafaray_createRenderControl();
    monitor = yafaray_createRenderMonitor(NULL, NULL, YAFARAY_DISPLAY_CONSOLE_HIDDEN);
    if(!p || !nodes || !scene || !control || !monitor) goto cleanup;
    if(!surface(scene, p, nodes, "floor", filter, 0, 2.0, 1, 0.75, 0.35, 0) ||
       !surface(scene, p, nodes, "ceiling", filter, 3, 3.4, 0.45, 0.75, 1, 1)) goto cleanup;

    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "constant");
    yafaray_setParamMapColor(p, "color", 0.2, 0.2, 0.2, 1);
    if(!ok(yafaray_defineBackground(scene, p))) goto cleanup;
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "directlighting");
    yafaray_setParamMapInt(p, "threads", threads);
    integrator = yafaray_createSurfaceIntegrator(logger, "texture_sampling", p);
    if(!integrator) goto cleanup;
    yafaray_clearParamMap(p);
    yafaray_setParamMapInt(p, "width", WIDTH);
    yafaray_setParamMapInt(p, "height", HEIGHT);
    yafaray_setParamMapInt(p, "AA_passes", 1);
    yafaray_setParamMapInt(p, "AA_minsamples", aa);
    film = yafaray_createFilm(logger, integrator, "horizon", p);
    if(!film) goto cleanup;
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "type", "perspective");
    yafaray_setParamMapInt(p, "resx", WIDTH);
    yafaray_setParamMapInt(p, "resy", HEIGHT);
    yafaray_setParamMapFloat(p, "focal", 1.0);
    yafaray_setParamMapVector(p, "from", 0, 0, 1.5);
    yafaray_setParamMapVector(p, "to", 0, 1, 1.5);
    yafaray_setParamMapVector(p, "up", 0, 0, 2.5);
    if(!ok(yafaray_defineCamera(film, p))) goto cleanup;
    yafaray_clearParamMap(p);
    yafaray_setParamMapString(p, "image_path", output);
    if(!ok(yafaray_createOutput(film, "tga", p))) goto cleanup;
    yafaray_setRenderControlForNormalStart(control);
    if(!yafaray_preprocessScene(scene, control, yafaray_checkAndClearSceneModifiedFlags(scene))) goto cleanup;
    yafaray_preprocessSurfaceIntegrator(monitor, integrator, control, scene);
    file = fopen(output, "wb");
    if(!file) { perror(output); goto cleanup; }
    if(fclose(file)) goto cleanup;
    yafaray_render(control, monitor, integrator, film);
    file = fopen(output, "rb");
    if(!file) goto cleanup;
    if(fread(header, 1, 18, file) != 18 || fseek(file, 0, SEEK_END))
    { fclose(file); goto cleanup; }
    size = ftell(file);
    fclose(file);
    if(header[2] != 2 || header[12] + 256 * header[13] != WIDTH ||
       header[14] + 256 * header[15] != HEIGHT || (header[16] != 24 && header[16] != 32) ||
       size < 18L + header[0] + (long)WIDTH * HEIGHT * (header[16] / 8)) goto cleanup;
    printf("Saved %s (%d x %d).\n", output, WIDTH, HEIGHT);
    status = EXIT_SUCCESS;
cleanup:
    if(status) fputs("Horizon render failed.\n", stderr);
    if(film) yafaray_destroyFilm(film);
    if(integrator) yafaray_destroySurfaceIntegrator(integrator);
    if(scene) yafaray_destroyScene(scene);
    if(monitor) yafaray_destroyRenderMonitor(monitor);
    if(control) yafaray_destroyRenderControl(control);
    if(nodes) yafaray_destroyParamMapList(nodes);
    if(p) yafaray_destroyParamMap(p);
    if(logger) yafaray_destroyLogger(logger);
    return status;
}
