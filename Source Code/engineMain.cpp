#include "stdafx.h"

#include "config.h"

#include "engineMain.h"

#include "Core/Headers/Application.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

bool TaskPool::USE_OPTICK_PROFILER = true;

class StreamBuffer {
public:
    explicit StreamBuffer(const char* filename)
        : _buf(std::ofstream(filename, std::ofstream::out | std::ofstream::trunc))
    {
    }

    std::ofstream& buffer() noexcept { return _buf; }

private:
    std::ofstream _buf;
};

Engine::Engine() 
    : _app(eastl::make_unique<Application>())
{
    std::set_new_handler([]() noexcept { DIVIDE_ASSERT(false, "Out of memory!"); });
    _outputStreams[0] = new StreamBuffer((Paths::g_logPath + OUTPUT_LOG_FILE).c_str());
    _outputStreams[1] = new StreamBuffer((Paths::g_logPath + ERROR_LOG_FILE).c_str());
    std::cout.rdbuf(_outputStreams[0]->buffer().rdbuf());
    std::cerr.rdbuf(_outputStreams[1]->buffer().rdbuf());
}

Engine::~Engine()
{
    std::cout << std::endl;
    std::cerr << std::endl;
    delete _outputStreams[0];
    delete _outputStreams[1];
}

bool Engine::init(const int argc, char** argv) {
    ErrorCode err = PlatformInit(argc, argv);
    if (err == ErrorCode::NO_ERR) {
        // Start our application based on XML configuration.
        // If it fails to start, it should automatically clear up all of its data
        err = _app->start("main.xml", argc, argv);
        if (err != ErrorCode::NO_ERR) {
            // If any error occurred, close the application as details should already be logged
            Console::errorfn("System failed to initialize properly. Error [ %s ] ", getErrorCodeName(err));
        }

    }

    _errorCode = to_I32(err);

    return err == ErrorCode::NO_ERR;
}

void Engine::shutdown() {
    _app->stop();

    if (!PlatformClose()) {
        _errorCode = to_I32(ErrorCode::PLATFORM_CLOSE_ERROR);
    }
}

bool Engine::step() const {
    assert(_errorCode == 0);

    return _app->step();
}

int Engine::errorCode() const {
    return to_I32(_errorCode);
}

}
