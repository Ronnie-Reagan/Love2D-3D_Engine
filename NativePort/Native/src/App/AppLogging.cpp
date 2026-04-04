#include "App/AppLogging.hpp"

#include "App/AppRunner.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace TrueFlightApp {

namespace {

std::mutex& stdoutLogMutex()
{
    static std::mutex mutex;
    return mutex;
}

const char* sdlLogPriorityLabel(SDL_LogPriority priority)
{
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE:
        return "verbose";
    case SDL_LOG_PRIORITY_DEBUG:
        return "debug";
    case SDL_LOG_PRIORITY_INFO:
        return "info";
    case SDL_LOG_PRIORITY_WARN:
        return "warn";
    case SDL_LOG_PRIORITY_ERROR:
        return "error";
    case SDL_LOG_PRIORITY_CRITICAL:
        return "critical";
    default:
        return "log";
    }
}

SDL_LogOutputFunction gPreviousSdlLogOutput = nullptr;
void* gPreviousSdlLogUserdata = nullptr;

void SDLCALL mirrorSdlLogToStdout(void*, int category, SDL_LogPriority priority, const char* message)
{
    {
        std::lock_guard<std::mutex> lock(stdoutLogMutex());
        std::cout << "[sdl][" << category << "][" << sdlLogPriorityLabel(priority) << "] "
                  << (message != nullptr ? message : "") << std::endl;
    }
    if (gPreviousSdlLogOutput != nullptr)
    {
        gPreviousSdlLogOutput(gPreviousSdlLogUserdata, category, priority, message);
    }
}

#ifdef _WIN32
LONG WINAPI trueFlightUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers)
{
    std::ostringstream stream;
    stream << "[fatal] unhandled structured exception 0x"
           << std::hex
           << std::uppercase
           << (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
                   ? exceptionPointers->ExceptionRecord->ExceptionCode
                   : 0u);
    logToStdout(stream.str());
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}  // namespace

void logToStdout(std::string_view message)
{
    std::lock_guard<std::mutex> lock(stdoutLogMutex());
    std::cout << message << std::endl;
}

void installStdoutLogMirror()
{
    std::cout.setf(std::ios::unitbuf);
    SDL_GetLogOutputFunction(&gPreviousSdlLogOutput, &gPreviousSdlLogUserdata);
    SDL_SetLogOutputFunction(&mirrorSdlLogToStdout, nullptr);
}

void installTerminateLogging()
{
    std::set_terminate([] {
        try
        {
            if (const std::exception_ptr current = std::current_exception(); current)
            {
                std::rethrow_exception(current);
            }
        }
        catch (const std::exception& exception)
        {
            logToStdout(std::string("[fatal] unhandled exception: ") + exception.what());
        }
        catch (...)
        {
            logToStdout("[fatal] unhandled non-standard exception");
        }
        std::abort();
    });
}

void installStructuredExceptionLogging()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(&trueFlightUnhandledExceptionFilter);
#endif
}

}  // namespace TrueFlightApp