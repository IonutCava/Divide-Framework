#include "stdafx.h"

#include "Headers/Defines.h"
#include "Core/Headers/ByteBuffer.h"
#include "Platform/Headers/PlatformDefines.h"

namespace Divide{

template<typename T>
bool compareVectors(const vector<T>& a, const vector<T>& b) {
    if (a.size() == b.size()) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }

    return false;
}

template<typename T, size_t N>
bool compareArrays(const std::array<T, N>& a, const std::array<T, N>& b) {
    for (size_t i = 0; i < N; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

TEST(ByteBufferRWBool)
{
    const bool input = false;

    ByteBuffer test;
    test << input;

    bool output = true;
    test >> output;
    CHECK_EQUAL(output, input);
}

TEST(ByteBufferRWPOD)
{
    const U8  inputU8 = 2;
    const U16 inputU16 = 4;
    const U32 inputU32 = 6;
    const I8  inputI8 = -2;
    const I16 inputI16 = 40;
    const I32 inputI32 = -6;
    const F32 inputF32 = 3.45632f;
    const D64 inputD64 = 1.14159;

    ByteBuffer test;
    test << inputU8;
    test << inputU16;
    test << inputU32;
    test << inputI8;
    test << inputI16;
    test << inputI32;
    test << inputF32;
    test << inputD64;

    U8  outputU8 = 0;
    U16 outputU16 = 0;
    U32 outputU32 = 0;
    I8  outputI8 = 0;
    I16 outputI16 = 0;
    I32 outputI32 = 0;
    F32 outputF32 = 0.0f;
    D64 outputD64 = 0.0;

    test >> outputU8;
    test >> outputU16;
    test >> outputU32;
    test >> outputI8;
    test >> outputI16;
    test >> outputI32;
    test >> outputF32;
    test >> outputD64;

    CHECK_EQUAL(outputU8,  inputU8);
    CHECK_EQUAL(outputU16, inputU16);
    CHECK_EQUAL(outputU32, inputU32);
    CHECK_EQUAL(outputI8,  inputI8);
    CHECK_EQUAL(outputI16, inputI16);
    CHECK_EQUAL(outputI32, inputI32);
    CHECK_TRUE(COMPARE(outputF32, inputF32));
    CHECK_TRUE(COMPARE(outputD64, inputD64));
}

TEST(ByteBufferSimpleMarker)
{
    constexpr std::array<U16, 3> testMarker{ 444u, 555u, 777u };

    constexpr U8 input = 122u;

    ByteBuffer test;
    test << string{ "StringTest Whatever" };
    test << U32{ 123456u };
    test.addMarker(testMarker);
    test << input;

    test.readSkipToMarker(testMarker);

    U8 output = 0u;
    test >> output;
    CHECK_EQUAL(input, output);
}

TEST(ByteBufferEvenNoMarker)
{
    constexpr std::array<U16, 3> testMarker{ 444u, 555u, 777u };

    ByteBuffer test;
    test << string{ "StringTest Whatever" };
    test << U32{ 123456u }; //Multiple of our marker size

    test.readSkipToMarker(testMarker);

    // Should just skip to the end
    CHECK_TRUE(test.bufferEmpty());
}

TEST(ByteBufferOddNoMarker)
{
    constexpr std::array<U16, 3> testMarker{ 444u, 555u, 777u };

    ByteBuffer test;
    test << string{ "StringTest Whatever" };
    test << U32{ 123456u }; //Multiple of our marker size
    test << U8{ 122u }; //Extra byte to check proper skipping

    test.readSkipToMarker(testMarker);

    // Should just skip to the end, but we use 16bit markers and we have 5 x 8bit values in the buffer
    // So we will fall short by a single byte. Hopefully, we skip it automatically
    CHECK_TRUE(test.bufferEmpty());
}

TEST(ByteBufferWrongMarker)
{
    std::array<U16, 3> testMarker{ 444u, 555u, 777u };

    ByteBuffer test;
    test << string{ "StringTest Whatever" };
    test << U32{ 123456u };
    test.addMarker(testMarker);
    test << U8{122u};

    testMarker[2] -= 1;
    test.readSkipToMarker(testMarker);

    // Should just skip to the end
    CHECK_TRUE(test.bufferEmpty());
}

TEST(ByteBufferRWString)
{
    const string input = "StringTest Whatever";

    ByteBuffer test;
    test << input;

    string output = "Output";

    test >> output;

    CHECK_EQUAL(input, output);
}


TEST(ByteBufferRWVectorInt)
{
    const vector<I32> input = { -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5 };

    ByteBuffer test;
    test << input;

    vector<I32> output;

    test >> output;

    CHECK_TRUE(compareVectors(input, output));
}


TEST(ByteBufferRWVectorString)
{
    const vector<string> input = { "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" };

    ByteBuffer test;
    test << input;

    vector<string> output;

    test >> output;

    CHECK_TRUE(compareVectors(input, output));
}

TEST(ByteBufferRWArrayInt)
{
    const std::array<I32, 11> input = { -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5 };

    ByteBuffer test;
    test << input;

    std::array<I32, 11> output{};

    test >> output;

    CHECK_TRUE(compareArrays(input, output));
}


TEST(ByteBufferRWArrayString)
{
    const std::array<string, 11> input = { "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" };

    ByteBuffer test;
    test << input;

    std::array<string, 11> output;

    test >> output;

    CHECK_TRUE(compareArrays(input, output));
}


TEST(ByteBufferRWMixedData)
{
    const bool inputBool = false;
    const vector<I32> inputVectorInt = { -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5 };
    const vector<string> inputVectorStr = { "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" };
    const std::array<I32, 11> inputArrayInt = { -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5 };
    const std::array<string, 11> inputArrayStr = { "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" };
    const U8  inputU8 = 2;
    const U16 inputU16 = 4;
    const U32 inputU32 = 6;
    const I8  inputI8 = -2;
    const I16 inputI16 = 40;
    const I32 inputI32 = -6;
    const F32 inputF32 = 3.45632f;
    const string inputStr = "StringTest Whatever";
    const D64 inputD64 = 1.14159;

    bool outputBool = true;
    vector<I32> outputVectorInt;
    vector<string> outputVectorStr;
    std::array<I32, 11> outputArrayInt{};
    std::array<string, 11> outputArrayStr;
    U8  outputU8 = 0;
    U16 outputU16 = 0;
    U32 outputU32 = 0;
    I8  outputI8 = 0;
    I16 outputI16 = 0;
    I32 outputI32 = 0;
    F32 outputF32 = 0.0f;
    string outputStr = "Output";
    D64 outputD64 = 0.0;

    ByteBuffer test;
    test << inputBool;
    test << inputVectorInt;
    test << inputVectorStr;
    test << inputArrayInt;
    test << inputArrayStr;
    test << inputU8;
    test << inputU16;
    test << inputU32;
    test << inputI8;
    test << inputI16;
    test << inputI32;
    test << inputF32;
    test << inputStr;
    test << inputD64;


    test >> outputBool;
    test >> outputVectorInt;
    test >> outputVectorStr;
    test >> outputArrayInt;
    test >> outputArrayStr;
    test >> outputU8;
    test >> outputU16;
    test >> outputU32;
    test >> outputI8;
    test >> outputI16;
    test >> outputI32;
    test >> outputF32;
    test >> outputStr;
    test >> outputD64;

    CHECK_EQUAL(inputBool, outputBool);
    CHECK_TRUE(compareVectors(inputVectorInt, outputVectorInt));
    CHECK_TRUE(compareVectors(inputVectorStr, outputVectorStr));
    CHECK_TRUE(compareArrays(inputArrayInt, outputArrayInt));
    CHECK_TRUE(compareArrays(inputArrayStr, outputArrayStr));
    CHECK_EQUAL(outputU8, inputU8);
    CHECK_EQUAL(outputU16, inputU16);
    CHECK_EQUAL(outputU32, inputU32);
    CHECK_EQUAL(outputI8, inputI8);
    CHECK_EQUAL(outputI16, inputI16);
    CHECK_EQUAL(outputI32, inputI32);
    CHECK_TRUE(COMPARE(outputF32, inputF32));
    CHECK_EQUAL(inputStr, outputStr);
    CHECK_TRUE(COMPARE(outputD64, inputD64));

}

}//namespace Divide