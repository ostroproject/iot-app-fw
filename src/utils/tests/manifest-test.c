#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/utils/manifest.h>


void dump_manifest(iot_manifest_t *m)
{
    char *apps[64], *privs[64], *args[64];
    const char *desktop, *descr;
    int   napp, npriv, narg, i, j;

    printf("manifest path: %s\n", iot_manifest_path(m));

    napp = iot_manifest_applications(m, apps, 64);
    printf("  %d apps:\n", napp);

    for (i = 0; i < napp; i++) {
        descr = iot_manifest_description(m, apps[i]);
        printf("    #%d: %s (%s)\n", i, apps[i], descr ? descr : "???");

        npriv = iot_manifest_privileges(m, apps[i], privs, 64);
        printf("      %d privileges:\n", npriv);
        for (j = 0; j < npriv; j++)
            printf("        #%d: %s\n", j, privs[j]);

        narg = iot_manifest_arguments(m, apps[i], args, 64);
        printf("      %d arguments:\n", narg);
        for (j = 0; j < narg; j++)
            printf("        #%d: %s\n", j, args[j]);

        desktop = iot_manifest_desktop_path(m, apps[i]);
        printf("      desktop file: %s\n", desktop ? desktop : "-");
    }
}



int main(int argc, char *argv[])
{
    iot_manifest_t *m;
    int             i;

    if (argc > 2)
        iot_manifest_set_directories(argv[1], NULL);
    else if (argc > 1)
        iot_manifest_set_directories(argv[1], argv[2]);

    if (argc > 3) {
        for (i = 3; i < argc; i++) {
            m = iot_manifest_get(getuid(), argv[i]);

            if (m == NULL) {
                iot_log_error("Failed to get manifest for '%s'.", argv[i]);
                exit(1);
            }

            dump_manifest(m);

            iot_manifest_unref(m);
        }
    }

    return 0;
}
