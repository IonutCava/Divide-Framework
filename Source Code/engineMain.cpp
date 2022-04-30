#include "stdafx.h"

#include "config.h"

#include "engineMain.h"

#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

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

bool Engine::init(const int argc, char** argv) {
    _app = eastl::make_unique<Application>();

    // Start our application based on XML configuration.
    // If it fails to start, it should automatically clear up all of its data
    ErrorCode err = _app->start("main.xml", argc, argv);
    if (err != ErrorCode::NO_ERR) {
        // If any error occurred, close the application as details should already be logged
        Console::errorfn("System failed to initialize properly. Error [ %s ] ", getErrorCodeName(err));
    }

    _errorCode = to_I32(err);

    return err == ErrorCode::NO_ERR;
}

void Engine::run(const int argc, char** argv) {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);


    std::set_new_handler([]() noexcept { Divide::DIVIDE_ASSERT(false, "Out of memory!"); });
    _outputStreams[0] = new StreamBuffer{(Paths::g_logPath + OUTPUT_LOG_FILE).c_str()};
    _outputStreams[1] = new StreamBuffer{ (Paths::g_logPath + ERROR_LOG_FILE).c_str() };

    std::cout.rdbuf(_outputStreams[0]->buffer().rdbuf());
    std::cerr.rdbuf(_outputStreams[1]->buffer().rdbuf());

    if (PlatformInit(argc, argv) == ErrorCode::NO_ERR) {
        U64 iterationCount = 0u;
        do {
            U64 stepCount = 0u;
            ++iterationCount;

            const auto started = std::chrono::high_resolution_clock::now();

            // Start the engine
            if (init(argc, argv)) {
                // Step the entire application
                while (_app->step(_restartEngineOnClose))
                {
                    ++stepCount;
                }
            }
            // Stop the app
            _app->stop();
            _app.reset();

            const auto done = std::chrono::high_resolution_clock::now();

            std::cout << "Divide engine shutdown after "
                      << stepCount
                      << " engine steps and " 
                      << iterationCount
                      << " restart(s). "
                      << "Total time: "
                      << std::chrono::duration_cast<std::chrono::seconds>(done - started).count()
                      << " seconds."
                      << std::endl;

        } while (_restartEngineOnClose && errorCode() == 0);
    }

    if (!PlatformClose()) {
        _errorCode = to_I32(ErrorCode::PLATFORM_CLOSE_ERROR);
    }

    std::cout << std::endl;
    std::cerr << std::endl;
    delete _outputStreams[0];
    delete _outputStreams[1];
}

int Engine::errorCode() const {
    return to_I32(_errorCode) * -1;
}

}
