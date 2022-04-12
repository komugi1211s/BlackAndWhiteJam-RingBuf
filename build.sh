
if [ ! -d "dist" ]; then
    echo "[Build]: Making dist directory."
    mkdir dist
fi

echo "[Build]: Building executables."
FILE='src/main.cpp'
# clang -g -Wall -fsanitize=address -o dist/compiled $FILE -lm -lGL -lGLEW -lglfw -lraylib -fno-caret-diagnostics
emcc -o game.html src/main.cpp -Os -Wall /usr/local/lib/libraylib.a -I./src -I/usr/local/include -L. -L/usr/local/lib -s TOTAL_MEMORY=67108864 -s USE_GLFW=3 -s ASYNCIFY --preload-file assets --shell-file ./shell.html -DPLATFORM_WEB

if [ -d "assets" ]; then
    if [ -d "dist/assets" ]; then
        echo "[Build]: Clearing Assets inside dist directory."
        rm -r ./dist/assets
    fi 
    echo "[Build]: Copying assets into dist directory."
    cp -r ./assets ./dist/assets
else
    echo "[Build]: WARNING - asset directory does not exist. skipping the copy of assets."
fi

