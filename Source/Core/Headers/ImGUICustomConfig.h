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
#ifndef DVD_IMGUI_CUSTOM_CONFIG_H_
#define DVD_IMGUI_CUSTOM_CONFIG_H_

namespace Divide {
	bool DebugBreak(const bool condition) noexcept;
	namespace Assert {
		extern bool DIVIDE_ASSERT_FUNC(const bool expression, std::string_view expressionStr, std::string_view file, int line, std::string_view failMessage) noexcept;
	};
};

namespace ImGui {
	void SetScrollHereY(float center_y_ratio);
	void SetNextFrameWantCaptureMouse(bool want_capture_mouse);

	inline void SetScrollHere() { SetScrollHereY(0.5f); }
	inline void CaptureMouseFromApp() { SetNextFrameWantCaptureMouse(true); }
};

#define IM_ASSERT(_EXPR) Divide::Assert::DIVIDE_ASSERT_FUNC(_EXPR, #_EXPR, __FILE__, __LINE__, "IMGUI_ASSERT")
#define IM_DEBUG_BREAK() Divide::DebugBreak(true)
#define AddBezierCurve AddBezierCubic
#endif //DVD_IMGUI_CUSTOM_CONFIG_H_
