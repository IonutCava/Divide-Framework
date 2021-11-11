/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _CONDITIONAL_WAIT_H_
#define _CONDITIONAL_WAIT_H_

namespace Divide {
class PlatformContext;

void InitConditionalWait(PlatformContext&) noexcept;
void PlatformContextIdleCall();

#define WAIT_FOR_CONDITION_2_ARGS(condition, yld)  \
{                                                  \
    assert_type<bool>(yld);                        \
                                                   \
    if (yld) {                                     \
        while (!(condition)) {                     \
            PlatformContextIdleCall();             \
            std::this_thread::yield();             \
        }                                          \
    } else {                                       \
        while(!(condition)) {}                     \
    }                                              \
}

#define WAIT_FOR_CONDITION_1_ARGS(condition) WAIT_FOR_CONDITION_2_ARGS(condition, true)

#define ___DETAIL_WAIT_FOR_CONDITION(...) EXP(GET_3RD_ARG(__VA_ARGS__, WAIT_FOR_CONDITION_2_ARGS, WAIT_FOR_CONDITION_1_ARGS, ))
#define WAIT_FOR_CONDITION(...) EXP(___DETAIL_WAIT_FOR_CONDITION(__VA_ARGS__)(__VA_ARGS__))

#define WAIT_FOR_CONDITION_TIMEOUT_3_ARGS(condition, timeoutMS, yld)   \
{                                                                      \
    assert_type<bool>(yld);                                            \
    assert_type<D64>(timeoutMS);                                       \
                                                                       \
    if (timeoutMS >= 0.0) {                                            \
        const D64 start = Time::ElapsedMilliseconds(true);             \
                                                                       \
        while (!(condition)) {                                         \
            PlatformContextIdleCall();                                 \
            if (Time::ElapsedMilliseconds(true) - start >= timeoutMS)  \
            {                                                          \
                break;                                                 \
            }                                                          \
                                                                       \
            if (yld) {                                                 \
                std::this_thread::yield();                             \
            }                                                          \
        }                                                              \
    } else {                                                           \
        WAIT_FOR_CONDITION(condition, yld);                            \
    }                                                                  \
}

#define WAIT_FOR_CONDITION_TIMEOUT_2_ARGS(condition, timeoutMS) WAIT_FOR_CONDITION_TIMEOUT_3_ARGS(condition, timeoutMS, true)
#define WAIT_FOR_CONDITION_TIMEOUT_1_ARGS(condition) WAIT_FOR_CONDITION_TIMEOUT_3_ARGS(condition, 1.0, true)

#define ___DETAIL_WAIT_FOR_CONDITION_TIMEOUT(...) EXP(GET_4TH_ARG(__VA_ARGS__, WAIT_FOR_CONDITION_TIMEOUT_3_ARGS, WAIT_FOR_CONDITION_TIMEOUT_2_ARGS, WAIT_FOR_CONDITION_TIMEOUT_1_ARGS, ))
#define WAIT_FOR_CONDITION_TIMEOUT(...) EXP(___DETAIL_WAIT_FOR_CONDITION_TIMEOUT(__VA_ARGS__)(__VA_ARGS__))

#define WAIT_FOR_CONDITION_CALLBACK_4_ARGS(condition, cbk, param, yld)   \
{                                                                        \
    assert_type<bool>(yld);                                              \
                                                                         \
    while (!(condition)) {                                               \
        cbk(param);                                                      \
        PlatformContextIdleCall();                                       \
        if (yld) {                                                       \
            std::this_thread::yield();                                   \
        }                                                                \
    }                                                                    \
}

#define WAIT_FOR_CONDITION_CALLBACK_3_ARGS(condition, cbk, param) WAIT_FOR_CONDITION_CALLBACK_4_ARGS(condition, cbk, param, true)
#define WAIT_FOR_CONDITION_CALLBACK_2_ARGS(condition, cbk) WAIT_FOR_CONDITION_CALLBACK_3_ARGS(condition, cbk, void, true)
#define WAIT_FOR_CONDITION_CALLBACK_1_ARGS(condition) WAIT_FOR_CONDITION(condition) 

#define ___DETAIL_WAIT_FOR_CONDITION_CALLBACK(...) EXP(GET_4TH_ARG(__VA_ARGS__, WAIT_FOR_CONDITION_CALLBACK_3_ARGS, WAIT_FOR_CONDITION_CALLBACK_2_ARGS, WAIT_FOR_CONDITION_CALLBACK_1_ARGS, ))
#define WAIT_FOR_CONDITION_CALLBACK(...) EXP(___DETAIL_WAIT_FOR_CONDITION_CALLBACK(__VA_ARGS__)(__VA_ARGS__))

#define WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_5_ARGS(condition, cbk, param, timeoutMS, yld)   \
{                                                                                           \
    assert_type<bool>(yld);                                                                 \
    assert_type<D64>(timeoutMS);                                                            \
                                                                                            \
    if ((timeoutMS) >= 0.0) {                                                               \
        const D64 start = Time::ElapsedMilliseconds(true);                                  \
                                                                                            \
        while (!(condition)) {                                                              \
            cbk(param);                                                                     \
            PlatformContextIdleCall();                                                      \
                                                                                            \
            if (Time::ElapsedMilliseconds(true) - start >= (timeoutMS)) {                   \
                break;                                                                      \
            }                                                                               \
                                                                                            \
            if (yld) {                                                                      \
                std::this_thread::yield();                                                  \
            }                                                                               \
        }                                                                                   \
    } else {                                                                                \
        WAIT_FOR_CONDITION_CALLBACK(condition, cbk, param, yld);                            \
    }                                                                                       \
}

#define WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_4_ARGS(condition, cbk, param, timeoutMS) WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_5_ARGS(condition, cbk, param, timeoutMS, true)
#define WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_3_ARGS(condition, cbk, param) WAIT_FOR_CONDITION_CALLBACK(condition, cbk, param)
#define WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_2_ARGS(condition, cbk) WAIT_FOR_CONDITION_CALLBACK(condition, cbk)
#define WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_1_ARGS(condition) WAIT_FOR_CONDITION(condition)

#define ___DETAIL_WAIT_FOR_CONDITION_CALLBACK_TIMEOUT(...) EXP(GET_5TH_ARG(__VA_ARGS__, WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_4_ARGS, WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_3_ARGS, WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_2_ARGS, WAIT_FOR_CONDITION_CALLBACK_TIMEOUT_1_ARGS, ))
#define WAIT_FOR_CONDITION_CALLBACK_TIMEOUT(...) EXP(___DETAIL_WAIT_FOR_CONDITION_CALLBACK_TIMEOUT(__VA_ARGS__)(__VA_ARGS__))

};

#endif

