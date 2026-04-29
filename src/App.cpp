// App.cpp
#include "App.h"


void App::begin() {
    
    console.begin(&hp);

    hp.connect(&Serial2, false, 16, 17);
    hp.setDebug(true);
}


void App::update() {

    console.update();
    
}

