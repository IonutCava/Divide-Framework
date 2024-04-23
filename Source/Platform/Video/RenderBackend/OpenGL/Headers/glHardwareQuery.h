/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_GL_HARDWARE_QUERY_H_
#define DVD_GL_HARDWARE_QUERY_H_

#include "glResources.h"

#include "Core/Headers/RingBuffer.h"
#include "Core/TemplateLibraries/Headers/Vector.h"

namespace Divide {

class GFXDevice;

class glHardwareQuery {
public:
    explicit glHardwareQuery() noexcept;

    void create( gl46core::GLenum queryType);
    void destroy();

    [[nodiscard]] bool isResultAvailable() const;
    [[nodiscard]] I64 getResult() const;
    [[nodiscard]] I64 getResultNoWait() const;

    [[nodiscard]] U32 getID() const noexcept { return _queryID; }
    [[nodiscard]] bool enabled() const noexcept { return _enabled; }
    void enabled(const bool state) noexcept { _enabled = state; }

protected:
    bool _enabled = true;
    U32 _queryID = 0u;
};

class glHardwareQueryRing final : public RingBufferSeparateWrite {

public:
    explicit glHardwareQueryRing(GFXDevice& context, gl46core::GLenum queryType, U16 queueLength, U32 id = 0);
    ~glHardwareQueryRing() override;

    void resize(U16 queueLength) override;

    U32 id() const noexcept { return _id; }

    void begin() const;
    void end() const;

    bool isResultAvailable() const {
        return readQuery().isResultAvailable();
    }

    I64 getResult() const {
        return readQuery().getResult();
    }

    I64 getResultNoWait() const {
        return readQuery().getResultNoWait();
    }

    gl46core::GLenum type() const noexcept {
        return _queryType;
    }
protected:
    const glHardwareQuery& readQuery() const;
    const glHardwareQuery& writeQuery() const;

protected:
    GFXDevice& _context;
    vector<glHardwareQuery> _queries;
    U32 _id = 0u;
    gl46core::GLenum _queryType = gl46core::GL_NONE;
};

FWD_DECLARE_MANAGED_CLASS(glHardwareQueryRing);

class glHardwareQueryPool {
public:
    explicit glHardwareQueryPool(GFXDevice& context);
    ~glHardwareQueryPool();

    void init(const hashMap<gl46core::GLenum, U32>& sizes);
    void destroy();

    glHardwareQueryRing& allocate( gl46core::GLenum queryType);
    void deallocate(const glHardwareQueryRing& query);

private:
    hashMap<gl46core::GLenum, vector<glHardwareQueryRing*>> _queryPool;
    hashMap<gl46core::GLenum, U32> _index;

    GFXDevice& _context;
};

}; //namespace Divide

#endif //DVD_GL_HARDWARE_QUERY_H_
