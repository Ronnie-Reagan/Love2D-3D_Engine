#include "App/AppRunner.hpp"

int main(int argc, char** argv)
try
{
    TrueFlightApp::installStdoutLogMirror();
    TrueFlightApp::installTerminateLogging();
    TrueFlightApp::installStructuredExceptionLogging();
    return TrueFlightApp::run(argc, argv);
}
catch (...)
{
    return 1;
}
