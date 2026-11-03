/* Simple C90 client: a red cube on a floor, rendered by the installed DLL.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yafaray_c_api.h>

#define WIDTH 480
#define HEIGHT 360

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
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "shinydiffusemat");
    yafaray_setParamMapColor(params, "color", r, g, b, 1.0);
    return succeeded(yafaray_createMaterial(scene, id, name, params, nodes));
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
    const char *output = argc > 1 ? argv[1] : "scene.tga";
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
    size_t output_length = strlen(output);

    if(argc > 2)
    {
        fprintf(stderr, "Usage: %s [output.tga]\n", argv[0]);
        return EXIT_FAILURE;
    }
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

    logger = yafaray_createLogger("render_scene", NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);
    if(!logger) goto cleanup;
    yafaray_setConsoleVerbosityLevel(logger, YAFARAY_LOG_LEVEL_INFO);
    params = yafaray_createParamMap();
    nodes = yafaray_createParamMapList();
    scene = yafaray_createScene(logger, "cube_on_floor");
    control = yafaray_createRenderControl();
    monitor = yafaray_createRenderMonitor(NULL, NULL, YAFARAY_DISPLAY_CONSOLE_HIDDEN);
    if(!params || !nodes || !scene || !control || !monitor) goto cleanup;

    /* Materials and geometry: no textures or external scene assets. */
    if(!material(scene, params, nodes, "red", 0.75, 0.08, 0.035, &cube_material) ||
       !material(scene, params, nodes, "floor", 0.55, 0.60, 0.65, &floor_material)) goto cleanup;
    if(!mesh(scene, params, "cube", cube, 8, cube_faces, 12, cube_material) ||
       !mesh(scene, params, "floor", floor_vertices, 4, floor_faces, 2, floor_material) ||
       !mesh(scene, params, "emitter", emitter_vertices, 4, floor_faces, 2, floor_material)) goto cleanup;

    yafaray_clearParamMap(params);
    /* A 3 x 3 area emitter. Edge ordering points its emitting side downward. */
    yafaray_setParamMapString(params, "type", "arealight");
    yafaray_setParamMapVector(params, "corner", -4.5, -5.5, 7.0);
    yafaray_setParamMapVector(params, "point1", -4.5, -2.5, 7.0);
    yafaray_setParamMapVector(params, "point2", -1.5, -5.5, 7.0);
    yafaray_setParamMapColor(params, "color", 1.0, 0.95, 0.85, 1.0);
    yafaray_setParamMapFloat(params, "power", 12.0);
    yafaray_setParamMapInt(params, "samples", 8);
    yafaray_setParamMapString(params, "object_name", "emitter");
    if(!succeeded(yafaray_createLight(scene, "key", params))) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "constant");
    yafaray_setParamMapColor(params, "color", 0.08, 0.10, 0.15, 1.0);
    if(!succeeded(yafaray_defineBackground(scene, params))) goto cleanup;

    /* Diffuse paths carry light reflected by the floor onto the cube. */
    yafaray_clearParamMap(params);
    yafaray_setParamMapString(params, "type", "pathtracing");
    yafaray_setParamMapInt(params, "path_samples", 8);
    yafaray_setParamMapInt(params, "bounces", 3);
    yafaray_setParamMapInt(params, "russian_roulette_min_bounces", 3);
    yafaray_setParamMapInt(params, "threads", 2);
    integrator = yafaray_createSurfaceIntegrator(logger, "path", params);
    if(!integrator) goto cleanup;

    yafaray_clearParamMap(params);
    yafaray_setParamMapInt(params, "width", WIDTH);
    yafaray_setParamMapInt(params, "height", HEIGHT);
    yafaray_setParamMapInt(params, "AA_passes", 1);
    yafaray_setParamMapInt(params, "AA_minsamples", 32);
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
