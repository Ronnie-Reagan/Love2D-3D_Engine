#pragma once

namespace TrueFlightApp {

void installStdoutLogMirror();
void installTerminateLogging();
void installStructuredExceptionLogging();
int run(int argc, char** argv);

}  // namespace TrueFlightApp
