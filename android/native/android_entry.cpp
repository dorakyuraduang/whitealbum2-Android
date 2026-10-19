#include "android_host.hpp"
#include <SDL_main.h>

extern "C" int SDL_main(int argc,char** argv){
    return wa2::android::run_game(argc,argv);
}
