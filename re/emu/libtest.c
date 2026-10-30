// Runs the app's engine build (real-time pacing) without a window: prints a hash of the screen twice a second.
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "../../app/Engine/engine.h"
int main(int argc, char **argv) {
    static unsigned char fb[320 * 200 * 4]; unsigned last = 0;
    engine_start(argv[1], argv[2]);
    for (int i = 0; i < atoi(argv[3]) * 10; i++) {
        usleep(100000); engine_copy_frame(fb);
        unsigned h = 2166136261u; for (int j = 0; j < sizeof fb; j++) h = (h ^ fb[j]) * 16777619u;
        if (h != last) printf("%5.1fs screen %08x\n", i / 10.0, h); last = h;
    }
    return 0;
}
