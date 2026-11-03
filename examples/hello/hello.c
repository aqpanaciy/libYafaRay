/* Minimal external C90 client of the installed libYafaRay shared library. */
#include <stdio.h>
#include <stdlib.h>
#include <yafaray_c_api.h>

int main(void)
{
    char *version;
    yafaray_ParamMap *params;

    if(yafaray_getVersionMajor() != YAFARAY_C_API_VERSION_MAJOR)
    {
        fputs("Incompatible libYafaRay API version.\n", stderr);
        return EXIT_FAILURE;
    }

    version = yafaray_getVersionString();
    if(!version) return EXIT_FAILURE;
    printf("libYafaRay %s\n", version);
    yafaray_destroyCharString(version);

    params = yafaray_createParamMap();
    if(!params) return EXIT_FAILURE;
    yafaray_setParamMapString(params, "name", "hello from xmake");
    yafaray_destroyParamMap(params);
    puts("C API: parameter map created and destroyed successfully.");
    return EXIT_SUCCESS;
}
