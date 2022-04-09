
if [ ! -d "dist" ]; then
    echo "[Build]: Making dist directory."
    mkdir dist
fi

echo "[Build]: Building executables."
FILE='src/main.cpp'
clang -g -Wall -fsanitize=address -o dist/compiled $FILE -lm -lGL -lGLEW -lglfw -lraylib -fno-caret-diagnostics

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

