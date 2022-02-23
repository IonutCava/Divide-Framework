#include "stdafx.h"

#include "Headers/glHardwareQuery.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

glHardwareQuery::glHardwareQuery(GFXDevice& context) noexcept
    : glObject(glObjectType::TYPE_QUERY, context),
      _enabled(false)
{
}

void glHardwareQuery::create([[maybe_unused]] const GLenum queryType) {
    destroy();
    glGenQueries(1, &_queryID);
}

void glHardwareQuery::destroy() {
    if (_queryID != 0u) {
        glDeleteQueries(1, &_queryID);
    }
    _queryID = 0u;
}

bool glHardwareQuery::isResultAvailable() const {
    GLint available = 0;
    glGetQueryObjectiv(getID(), GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
}

I64 glHardwareQuery::getResult() const {
    GLint64 res = 0;
    glGetQueryObjecti64v(getID(), GL_QUERY_RESULT, &res);
    return res;
}

I64 glHardwareQuery::getResultNoWait() const {
    GLint64 res = 0;
    glGetQueryObjecti64v(getID(), GL_QUERY_RESULT_NO_WAIT, &res);
    return res;
}

glHardwareQueryRing::glHardwareQueryRing(GFXDevice& context, const GLenum queryType, const U32 queueLength, const U32 id)
    : RingBufferSeparateWrite(queueLength, true),
      _context(context),
      _id(id),
      _queryType(queryType)
{
    _queries.reserve(queueLength);
    resize(queueLength);
}

glHardwareQueryRing::~glHardwareQueryRing()
{
    for (glHardwareQuery& query : _queries) {
        query.destroy();
    }
    _queries.clear();
}

const glHardwareQuery& glHardwareQueryRing::readQuery() const {
    return _queries[queueReadIndex()];
}

const glHardwareQuery& glHardwareQueryRing::writeQuery() const {
    return _queries[queueWriteIndex()];
}

void glHardwareQueryRing::resize(const U32 queueLength) {
    RingBufferSeparateWrite::resize(queueLength);

    const size_t crtCount = _queries.size();
    if (queueLength < crtCount) {
        while (queueLength < crtCount) {
            _queries.back().destroy();
            _queries.pop_back();
        }
    } else {
        const size_t countToAdd = queueLength - crtCount;

        for (size_t i = 0; i < countToAdd; ++i) {
            _queries.emplace_back(_context);
            _queries.back().create(_queryType);

            //Prime the query
            glBeginQuery(_queryType, _queries.back().getID());
            glEndQuery(_queryType);
        }
    }
}


void glHardwareQueryRing::begin() const {
    glBeginQuery(_queryType, writeQuery().getID());
}

void glHardwareQueryRing::end() const {
    glEndQuery(_queryType);
}
}; //namespace Divide