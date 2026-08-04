#include "karyon.h"

static void load(const krn::EngineCtx& ctx)
{

}

int main()
{
    krn::EngineSettings engineSettings { .loadCallback = load };
    krn::WindowSettings windowSettings { };
    krn::Engine karyon { engineSettings, windowSettings };

    karyon.Run();
}
