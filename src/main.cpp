#include <raylib.h>
#include <stdlib.h>

#define FUZZY_MY_H_IMPL
#include "my.h"

int main(void) {
    InitWindow(800, 600, "MainWindow");

    while(!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
