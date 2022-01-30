#include "stdafx.h"

#include "Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
namespace GLUtil {

/// Print OpenGL specific messages
void DebugCallback(const GLenum source,
                   const GLenum type,
                   const GLuint id,
                   const GLenum severity,
                   [[maybe_unused]] const GLsizei length,
                   const GLchar* message,
                   const void* userParam) {

    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
        // Translate message source
        const char* gl_source = "Unknown Source";
        if (source == GL_DEBUG_SOURCE_API) {
            gl_source = "OpenGL";
        } else if (source == GL_DEBUG_SOURCE_WINDOW_SYSTEM) {
            gl_source = "Windows";
        } else if (source == GL_DEBUG_SOURCE_SHADER_COMPILER) {
            gl_source = "Shader Compiler";
        } else if (source == GL_DEBUG_SOURCE_THIRD_PARTY) {
            gl_source = "Third Party";
        } else if (source == GL_DEBUG_SOURCE_APPLICATION) {
            gl_source = "Application";
        } else if (source == GL_DEBUG_SOURCE_OTHER) {
            gl_source = "Other";
        }
        // Translate message type
        const char* gl_type = "Unknown Type";
        if (type == GL_DEBUG_TYPE_ERROR) {
            gl_type = "Error";
        } else if (type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR) {
            gl_type = "Deprecated behavior";
        } else if (type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) {
            gl_type = "Undefined behavior";
        } else if (type == GL_DEBUG_TYPE_PORTABILITY) {
            gl_type = "Portability";
        } else if (type == GL_DEBUG_TYPE_PERFORMANCE) {
            gl_type = "Performance";
        } else if (type == GL_DEBUG_TYPE_OTHER) {
            gl_type = "Performance";
        } else if (type == GL_DEBUG_TYPE_MARKER) {
            gl_type = "Marker";
        } else if (type == GL_DEBUG_TYPE_PUSH_GROUP) {
            gl_type = "Push";
        } else if (type == GL_DEBUG_TYPE_POP_GROUP) {
            gl_type = "Pop";
        }

        // Translate message severity
        const char* gl_severity = "Unknown Severity";
        if (severity == GL_DEBUG_SEVERITY_HIGH) {
            gl_severity = "High";
        } else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
            gl_severity = "Medium";
        } else if (severity == GL_DEBUG_SEVERITY_LOW) {
            gl_severity = "Low";
        } else if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
            gl_severity = "Info";
        }

        std::string fullScope = "GL";
        for (U8 i = 0u; i < GL_API::GetStateTracker()._debugScopeDepth; ++i) {
            fullScope.append("::");
            fullScope.append(GL_API::GetStateTracker()._debugScope[i]);
        }
        // Print the message and the details
        const string outputError = Util::StringFormat(
            "[%s Thread][Source: %s][Type: %s][ID: %d][Severity: %s][Bound Program : %d][Bound Pipeline : %d][DebugGroup: %s][Message: %s]",
            userParam == nullptr ? "Main" : "Worker",
            gl_source,
            gl_type,
            id, 
            gl_severity, 
            GL_API::GetStateTracker()._activeShaderProgram,
            GL_API::GetStateTracker()._activeShaderPipeline,
            fullScope.c_str(),
            message);

        const bool isConsoleIM = Console::immediateModeEnabled();
        Console::toggleImmediateMode(true);
        Console::errorfn(outputError.c_str());
        Console::toggleImmediateMode(isConsoleIM);
    }

}
};  // namespace GLUtil
};  // namespace Divide