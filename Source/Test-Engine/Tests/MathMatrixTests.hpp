// make sure mat4 test include separe floating point and integer calls
// floating point mat4 uses SSE for performance reasons and results might differ
namespace Divide
{

TEST(matNSizeTest)
{
    const mat2<I8>  a1;
    const mat2<U8>  a2;
    const mat2<I16> a3;
    const mat2<U16> a4;
    const mat2<I32> a5;
    const mat2<U32> a6;
    const mat2<I64> a7;
    const mat2<U64> a8;
    const mat2<F32> a9;
    const mat2<D64> a10;

    CHECK_EQUAL(sizeof(a5), (32 * 2 * 2) / 8);

    CHECK_EQUAL(sizeof(a7), sizeof(vec2<I64>) * 2);

    CHECK_EQUAL(sizeof(a1),  sizeof(I8)  * 2 * 2);
    CHECK_EQUAL(sizeof(a2),  sizeof(U8)  * 2 * 2);
    CHECK_EQUAL(sizeof(a3),  sizeof(I16) * 2 * 2);
    CHECK_EQUAL(sizeof(a4),  sizeof(U16) * 2 * 2);
    CHECK_EQUAL(sizeof(a5),  sizeof(I32) * 2 * 2);
    CHECK_EQUAL(sizeof(a6),  sizeof(U32) * 2 * 2);
    CHECK_EQUAL(sizeof(a7),  sizeof(I64) * 2 * 2);
    CHECK_EQUAL(sizeof(a8),  sizeof(U64) * 2 * 2);
    CHECK_EQUAL(sizeof(a9),  sizeof(F32) * 2 * 2);
    CHECK_EQUAL(sizeof(a10), sizeof(D64) * 2 * 2);
    CHECK_EQUAL(sizeof(a10), sizeof(a9)  * 2);

    const mat3<I8>  b1;
    const mat3<U8>  b2;
    const mat3<I16> b3;
    const mat3<U16> b4;
    const mat3<I32> b5;
    const mat3<U32> b6;
    const mat3<I64> b7;
    const mat3<U64> b8;
    const mat3<F32> b9;
    const mat3<D64> b10;

    CHECK_EQUAL(sizeof(b5), (32 * 3 * 3) / 8);

    CHECK_EQUAL(sizeof(b7), sizeof(vec3<I64>) * 3);

    CHECK_EQUAL(sizeof(b1),  sizeof(I8)  * 3 * 3);
    CHECK_EQUAL(sizeof(b2),  sizeof(U8)  * 3 * 3);
    CHECK_EQUAL(sizeof(b3),  sizeof(I16) * 3 * 3);
    CHECK_EQUAL(sizeof(b4),  sizeof(U16) * 3 * 3);
    CHECK_EQUAL(sizeof(b5),  sizeof(I32) * 3 * 3);
    CHECK_EQUAL(sizeof(b6),  sizeof(U32) * 3 * 3);
    CHECK_EQUAL(sizeof(b7),  sizeof(I64) * 3 * 3);
    CHECK_EQUAL(sizeof(b8),  sizeof(U64) * 3 * 3);
    CHECK_EQUAL(sizeof(b9),  sizeof(F32) * 3 * 3);
    CHECK_EQUAL(sizeof(b10), sizeof(D64) * 3 * 3);
    CHECK_EQUAL(sizeof(b10), sizeof(b9)  * 2);

    const mat4<I8>  c1;
    const mat4<U8>  c2;
    const mat4<I16> c3;
    const mat4<U16> c4;
    const mat4<I32> c5;
    const mat4<U32> c6;
    const mat4<I64> c7;
    const mat4<U64> c8;
    const mat4<F32> c9;
    const mat4<D64> c10;

    CHECK_EQUAL(sizeof(c5), (32 * 4 * 4) / 8);

    CHECK_EQUAL(sizeof(c7), sizeof(vec4<I64>) * 4);
    CHECK_EQUAL(sizeof(c9), sizeof(vec4<F32>) * 4);
    CHECK_EQUAL(sizeof(c3), sizeof(a3) * 4);

    CHECK_EQUAL(sizeof(c1),  sizeof(I8)  * 4 * 4);
    CHECK_EQUAL(sizeof(c2),  sizeof(U8)  * 4 * 4);
    CHECK_EQUAL(sizeof(c3),  sizeof(I16) * 4 * 4);
    CHECK_EQUAL(sizeof(c4),  sizeof(U16) * 4 * 4);
    CHECK_EQUAL(sizeof(c5),  sizeof(I32) * 4 * 4);
    CHECK_EQUAL(sizeof(c6),  sizeof(U32) * 4 * 4);
    CHECK_EQUAL(sizeof(c7),  sizeof(I64) * 4 * 4);
    CHECK_EQUAL(sizeof(c8),  sizeof(U64) * 4 * 4);
    CHECK_EQUAL(sizeof(c9),  sizeof(F32) * 4 * 4);
    CHECK_EQUAL(sizeof(c10), sizeof(D64) * 4 * 4);
    CHECK_EQUAL(sizeof(b10), sizeof(b9)  * 2);
}

TEST(matNUnionTests)
{
    mat2<I32> input1;
    mat3<U8> input2;
    mat4<I16> input3;
    mat4<F32> input4;

    // Random, unique values
    input1.set(-1, 0, 1, 2);
    input2.set(1, 2, 3, 4, 5, 6, 7, 8, 9);

    input3.set({ -8, -7, -6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7, 8 });
    for (U8 i = 0; i < 16; ++i) {
        input4.mat[i] = 22.345f * (i + 1);
    }

    // Quick constructor check
    CHECK_EQUAL(input2.element(1, 2), 6);
    // Check that element is just a direct access to m-member
    CHECK_EQUAL(input1.element(1, 0), input1.m[1][0]);
    CHECK_EQUAL(input2.element(2, 1), input2.m[2][1]);
    CHECK_EQUAL(input3.element(3, 2), input3.m[3][2]);
    CHECK_EQUAL(input4.element(2, 2), input4.m[2][2]);

    U8 row = 0u, column = 0u;
    U8 elementsPerLine = 2;
    for (row = 0; row < elementsPerLine; ++row) {
        for (column = 0; column < elementsPerLine; ++column) {
            CHECK_EQUAL(input1.element(row, column), input1.mat[row * elementsPerLine + column]);
        }
    }
    elementsPerLine = 3;
    for (row = 0; row < elementsPerLine; ++row) {
        for (column = 0; column < elementsPerLine; ++column) {
            CHECK_EQUAL(input2.element(row, column), input2.mat[row * elementsPerLine + column]);
        }
    }
    elementsPerLine = 4;
    for (row = 0; row < elementsPerLine; ++row) {
        for (column = 0; column < elementsPerLine; ++column) {
            CHECK_EQUAL(input3.element(row, column), input3.mat[row * elementsPerLine + column]);
        }
    }

    for (row = 0; row < elementsPerLine; ++row) {
        for (column = 0; column < elementsPerLine; ++column) {
            CHECK_EQUAL(input4.element(row, column), input4.mat[row * elementsPerLine + column]);
        }
    }
}

TEST_CONST_MEMBER_FUNCTION(matN, getCol, NA)
{
    const mat2<I32> input1(4, 4,
                           10, 8);
    const mat3<I32> input2(1, 2, 3,
                           4, 5, 6,
                           7, 8, 9);
    const mat4<F32> input3({1.0f, 2.0f, 3.0f, 4.0f,
                            5.0f, 6.0f, 7.0f, 8.0f,
                            9.0f, 10.0f, 11.0f, 12.0f,
                            13.0f, 14.0f, 15.0f, 16.0f});

    const vec2<I32> result1A(4, 10);
    const vec2<I32> result1B(4, 8);

    const vec3<I32> result2A(1, 4, 7);
    const vec3<I32> result2B(2, 5, 8);
    const vec3<I32> result2C(3, 6, 9);
    
    const vec4<F32> result3A(1.0f, 5.0f, 9.0f, 13.0f);
    const vec4<F32> result3B(2.0f, 6.0f, 10.0f, 14.0f);
    const vec4<F32> result3C(3.0f, 7.0f, 11.0f, 15.0f);
    const vec4<F32> result3D(4.0f, 8.0f, 12.0f, 16.0f);

    CHECK_EQUAL(input1.getCol(0), result1A);
    CHECK_EQUAL(input1.getCol(1), result1B);

    CHECK_EQUAL(input2.getCol(0), result2A);
    CHECK_EQUAL(input2.getCol(1), result2B);
    CHECK_EQUAL(input2.getCol(2), result2C);

    CHECK_EQUAL(input3.getCol(0), result3A);
    CHECK_EQUAL(input3.getCol(1), result3B);
    CHECK_EQUAL(input3.getCol(2), result3C);
    CHECK_EQUAL(input3.getCol(3), result3D);

}

TEST_CONST_MEMBER_FUNCTION(matN, getRow, NA)
{
    const mat2<I32> input1(4, 4,
                           10, 8);
    const mat3<I32> input2(1, 2, 3,
                           4, 5, 6,
                           7, 8, 9);
    const mat4<F32> input3({1.0f, 2.0f, 3.0f, 4.0f,
                            5.0f, 6.0f, 7.0f, 8.0f,
                            9.0f, 10.0f, 11.0f, 12.0f,
                            13.0f, 14.0f, 15.0f, 16.0f});


    const vec2<I32> result1A(4, 4);
    const vec2<I32> result1B(10, 8);

    const vec3<I32> result2A(1, 2, 3);
    const vec3<I32> result2B(4, 5, 6);
    const vec3<I32> result2C(7, 8, 9);

    const vec4<F32> result3A(1.0f, 2.0f, 3.0f,  4.0f);
    const vec4<F32> result3B(5.0f, 6.0f, 7.0f, 8.0f);
    const vec4<F32> result3C(9.0f, 10.0f, 11.0f, 12.0f);
    const vec4<F32> result3D(13.0f, 14.0f, 15.0f, 16.0f);

    CHECK_EQUAL(input1.getRow(0), result1A);
    CHECK_EQUAL(input1.getRow(1), result1B);

    CHECK_EQUAL(input2.getRow(0), result2A);
    CHECK_EQUAL(input2.getRow(1), result2B);
    CHECK_EQUAL(input2.getRow(2), result2C);

    CHECK_EQUAL(input3.getRow(0), result3A);
    CHECK_EQUAL(input3.getRow(1), result3B);
    CHECK_EQUAL(input3.getRow(2), result3C);
    CHECK_EQUAL(input3.getRow(3), result3D);
}

TEST_CONST_MEMBER_FUNCTION(matN, equalityOperator, NA)
{
    mat2<I32> input1[2];
    mat3<U8>  input2[2];
    mat4<I16> input3[2];
    mat4<F32> input4[2];

    // Random, unique values
    input1[0].set(-1, 0, 1, 2);
    input1[1].set(-1, 0, 1, 2);
    CHECK_TRUE(input1[0] == input1[1]);
    CHECK_FALSE(input1[0] != input1[1]);
    input1[1].mat[2] = 2;
    CHECK_TRUE(input1[0] != input1[1]);
    CHECK_FALSE(input1[0] == input1[1]);

    input2[0].set(1, 2, 3, 4, 5, 6, 7, 8, 9);
    input2[1].set(1, 2, 3, 4, 5, 6, 7, 8, 9);

    CHECK_TRUE(input2[0] == input2[1]);
    CHECK_FALSE(input2[0] != input2[1]);
    input2[1].m[2][0] = 22;
    CHECK_TRUE(input2[0] != input2[1]);
    CHECK_FALSE(input2[0] == input2[1]);

    input3[0].set({ -8, -7, -6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7, 8 });
    input3[1].set({ -8, -7, -6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7, 8 });
    CHECK_TRUE(input3[0] == input3[1]);
    CHECK_FALSE(input3[0] != input3[1]);
    input3[1].m[1][3] = 22;
    CHECK_TRUE(input3[0] != input3[1]);
    CHECK_FALSE(input3[0] == input3[1]);

    for (U8 i = 0; i < 2; ++i) {
        for (U8 j = 0; j < 16; ++j) {
            input4[i].mat[j] = 22.345f * j * (j % 2 == 0 ? -1.0f : 1.0f);
        }
    }
    CHECK_TRUE(input4[0] == input4[1]);
    CHECK_FALSE(input4[0] != input4[1]);
    
    for (U8 i = 0; i < 2; ++i) {
        for (U8 j = 0; j < 16; ++j) {
            input4[i].mat[j] = 22.345f * (j + i + 1);
        }
    }

    CHECK_TRUE(input4[0] != input4[1]);
    CHECK_FALSE(input4[0] == input4[1]);
}

TEST_MEMBER_FUNCTION(matN, identity, NA)
{
    mat2<F32> input1; input1.zero();
    const mat2<F32> result1(1.0f, 0.0f,
                            0.0f, 1.0f);
    CHECK_NOT_EQUAL(result1, input1);
    input1.identity();
    CHECK_TRUE(result1 == input1);

    mat3<F32> input2; input2.zero();
    const mat3<F32> result2(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);

    CHECK_NOT_EQUAL(result2, input2);
    input2.identity();
    CHECK_EQUAL(result2, input2);

    mat4<F32> input3; input3.zero();
    const mat4<F32> result3({1.0f, 0.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 0.0f, 1.0f});
    CHECK_NOT_EQUAL(result3, input3);
    input3.identity();
    CHECK_EQUAL(result3, input3);
}

TEST_MEMBER_FUNCTION(matN, transpose, NA)
{
    mat2<F32> input1(1.0f, 2.0f, 3.0f, 4.0f);
    const mat2<F32> result1(1.0f, 3.0f, 2.0f, 4.0f);
    const mat2<F32> result2(1.0f, 2.0f, 3.0f, 4.0f);
    CHECK_TRUE(input1.getTranspose() == result1);
    CHECK_TRUE(input1 != result1);
    input1.transpose();
    CHECK_TRUE(input1 == result1);
    input1.transpose();
    CHECK_TRUE(input1 == result2);


    mat3<I32> input2(1, 2, 3, 4, 5, 6, 7, 8, 9);
    const mat3<I32> result3(1, 4, 7, 2, 5, 8, 3, 6, 9);
    const mat3<I32> result4(1, 2, 3, 4, 5, 6, 7, 8, 9);
    CHECK_TRUE(input2.getTranspose() == result3);
    CHECK_TRUE(input2 != result3);
    input2.transpose();
    CHECK_TRUE(input2 == result3);
    input2.transpose();
    CHECK_TRUE(input2 == result4);


    mat4<F32> input3({1.0f, 2.0f, 3.0f, 4.0f,
                      5.0f, 6.0f, 7.0f, 8.0f,
                      9.0f, 10.0f, 11.0f, 12.0f,
                      13.0f, 14.0f, 15.0f, 16.0f});
    const mat4<F32> result5({1.0f, 5.0f, 9.0f, 13.0f,
                             2.0f, 6.0f, 10.0f, 14.0f,
                             3.0f, 7.0f, 11.0f, 15.0f,
                             4.0f, 8.0f, 12.0f, 16.0f});
    const mat4<F32> result6({1.0f, 2.0f, 3.0f, 4.0f,
                             5.0f, 6.0f, 7.0f, 8.0f,
                             9.0f, 10.0f, 11.0f, 12.0f,
                             13.0f, 14.0f, 15.0f, 16.0f});
    CHECK_TRUE(input3.getTranspose() == result5);
    CHECK_TRUE(input3 != result5);
    input3.transpose();
    CHECK_TRUE(input3 == result5);
    input3.transpose();
    CHECK_TRUE(input3 == result6);
}

TEST_MEMBER_FUNCTION(matN, multiplyOperator, scalar)
{
    const mat2<I32> input1(-2, -1,
                            1,  2);

    const mat2<I32> result1(-4, -2,
                             2,  4);

    CHECK_FALSE(input1 == result1);
    CHECK_TRUE((input1 * 2) == result1);


    const mat3<F32> input2(-5.0f, -4.0f, -3.0f, 
                           -2.0f, -1.0f,  1.0f,
                            2.0f,  3.0f,  4.0f);
    const mat3<F32> result2( 2.5f,  2.0f,  1.5f,
                             1.0f,  0.5f, -0.5f,
                            -1.0f, -1.5f, -2.0f);

    CHECK_FALSE(input2 == result2);
    CHECK_TRUE((input2 * (-0.5f)) == result2);


    const mat4<I32> input3({1, 2, 3, 4,
                            5, 6, 7, 8,
                            9, 8, 7, 6,
                            5, 4, 3, 2});
    const mat4<I32> result3({ 3,  6,  9, 12,
                             15, 18, 21, 24,
                             27, 24, 21, 18,
                             15, 12,  9,  6});

    CHECK_FALSE(input3 == result3);
    CHECK_TRUE((input3 * 3) == result3);

    const mat4<F32> input4({1.0f, 2.0f, 3.0f, 4.0f,
                            5.0f, 6.0f, 7.0f, 8.0f,
                            9.0f, 8.0f, 7.0f, 6.0f,
                            5.0f, 4.0f, 3.0f, 2.0f});

    const mat4<F32> result4A({ 3.0f,  6.0f,  9.0f, 12.0f,
                              15.0f, 18.0f, 21.0f, 24.0f,
                              27.0f, 24.0f, 21.0f, 18.0f,
                              15.0f, 12.0f,  9.0f,  6.0f});

    const mat4<F32> result4B({ 0.5f,  1.0f, 1.5f,  2.0f,
                               2.5f,  3.0f, 3.5f,  4.0f,
                               4.5f,  4.0f, 3.5f,  3.0f,
                               2.5f,  2.0f, 1.5f,  1.0f });

    CHECK_FALSE(input4 == result4A);

    const mat4<F32> something(input4);
    CHECK_EQUAL(something, input4);
    CHECK_EQUAL((something * 3), result4A);
    CHECK_EQUAL((something / 2), result4B);
}

TEST_MEMBER_FUNCTION(mat4, addSubtractOperator, scalar)
{
    const mat4<F32> input({1.0f, 2.0f, 3.0f, 4.0f,
                           5.0f, 6.0f, 7.0f, 8.0f,
                           9.0f, 8.0f, 7.0f, 6.0f,
                           5.0f, 4.0f, 3.0f, 2.0f});

    const mat4<F32> resultA({-2.0f, -1.0f, 0.0f,  1.0f,
                              2.0f,  3.0f, 4.0f,  5.0f,
                              6.0f,  5.0f, 4.0f,  3.0f,
                              2.0f,  1.0f, 0.0f, -1.0f});

    const mat4<F32> resultB({ 4.0f,  5.0f,  6.0f,  7.0f,
                              8.0f,  9.0f, 10.0f, 11.0f,
                             12.0f, 11.0f, 10.0f,  9.0f,
                              8.0f,  7.0f,  6.0f,  5.0f });

    CHECK_TRUE((input - 3) == resultA);
    CHECK_TRUE((input + 3) == resultB);
}

// this depends on multiply tests!
TEST_MEMBER_FUNCTION(matN, divideOperator, scalar)
{
    const mat2<I32> input1(-2, -2,
                            2,  2);
    CHECK_FALSE((input1 / 2) == (input1 * 2));
    CHECK_TRUE((input1 / 2) == (input1 * 0.5f));

    const mat3<F32> input2(-5.0f, -4.0f, -3.0f,
                           -2.0f, -1.0f, 1.0f,
                            2.0f, 3.0f, 4.0f);
    CHECK_FALSE((input2 / 2) == (input2 * 2));
    CHECK_TRUE((input2 / 2) == (input2 * 0.5f));

    const mat4<I32> input3({1, 2, 3, 4,
                            5, 6, 7, 8,
                            9, 8, 7, 6,
                            5, 4, 3, 2});
    CHECK_FALSE((input3 / 2) == (input3 * 2));

    CHECK_TRUE((input3 / 2) == (input3 * 0.5f));

    const mat4<F32> input4({1.0f, 2.0f, 3.0f, 4.0f,
                            5.0f, 6.0f, 7.0f, 8.0f,
                            9.0f, 8.0f, 7.0f, 6.0f,
                            5.0f, 4.0f, 3.0f, 2.0f});
    CHECK_FALSE((input4 / 2) == (input4 * 2));
    CHECK_EQUAL((input4 / 2), (input4 * 0.5f));
}

TEST_MEMBER_FUNCTION(mat4, Reflect, Plane)
{
    const vec3<F32> input(2, 2, 2);
    const vec3<F32> result(2, -2, 2);

    const Plane<F32> reflectPlane1(vec3<F32>(0.0f, 1.0f, 0.0f), 0.0f);
    const Plane<F32> reflectPlane2(vec3<F32>(0.0f, -1.0f, 0.0f), 0.0f);

    mat4<F32> reflectMat;
    reflectMat.reflect(reflectPlane1);

    CHECK_TRUE(result == reflectMat * input);

    reflectMat.reflect(reflectPlane2);

    CHECK_TRUE(input == reflectMat * input);
}

TEST_MEMBER_FUNCTION(matN, multiplyOperator, matN)
{
    mat2<I32> inputIdentity2x2;
    inputIdentity2x2.identity();

    const mat2<I32> input1A(1, 2,
                            3, 4);
    const mat2<I32> input1B(2, 0,
                            1, 2);
    const mat2<I32> result1A( 4, 4,
                             10, 8);
    const mat2<I32> result1B(2,  4,
                             7, 10);
    CHECK_EQUAL(input1A * input1B, result1A);
    CHECK_NOT_EQUAL(input1B * input1A, result1A);

    CHECK_NOT_EQUAL(input1A * input1B, result1B);
    CHECK_EQUAL(input1B * input1A, result1B);

    CHECK_EQUAL(input1A * inputIdentity2x2, input1A);


    mat3<I32> inputIdentity3x3;
    inputIdentity3x3.identity();

    const mat3<F32> input2A( 5.0f,  2.0f,  6.0f,
                            23.0f, 29.0f, 11.0f,
                            64.0f,  0.0f, 32.0f);
                
    const mat3<F32> input2B(24.0f, 92.0f, 112.0f,
                            5.0f,  0.0f,  95.0f,
                            43.0f, 2.0f,   9.0f);
               
    const mat3<F32> result2A( 388.0f,  472.0f,  804.0f,
                             1170.0f, 2138.0f, 5430.0f,
                             2912.0f, 5952.0f, 7456.0f);
                
    const mat3<F32> result2B(9404.0f, 2716.0f, 4740.0f,
                             6105.0f,   10.0f, 3070.0f,
                              837.0f,  144.0f,  568.0f);

    CHECK_EQUAL(input2A * input2B, result2A);
    CHECK_NOT_EQUAL(input2B * input2A, result2A);

    CHECK_NOT_EQUAL(input2A * input2B, result2B);
    CHECK_EQUAL(input2B * input2A, result2B);

    CHECK_EQUAL(input2A * inputIdentity3x3, input2A);
    

    mat4<I32> inputIdentity4x4;
    inputIdentity4x4.identity();

    const mat4<I32> input3A({ 5,  2,  6, 8,
                             23, 29, 11, 5,
                             64,  0, 32, 8,
                             -2,  5,  7, 1 });
                
    const mat4<I32> input3B({24, 92, 112,  4,
                              5,  0,  95,  2,
                             43,  2,   9,  9,
                              5,  7,  11, 67});

    const mat4<I32> result3A({ 428,  528,  892,  614,
                              1195, 2173, 5485,  584,
                              2952, 6008, 7544, 1080,
                               283, -163,  325,  132 });

    const mat4<I32> result3B({9396, 2736, 4768, 1552,
                              6101,   20, 3084,  802,
                               819,  189,  631,  435,
                               756,  548,  928,  230});

    CHECK_EQUAL(input3A * input3B, result3A);
    CHECK_NOT_EQUAL(input3B * input3A, result3A);

    CHECK_NOT_EQUAL(input3A * input3B, result3B);
    CHECK_EQUAL(input3B * input3A, result3B);

    CHECK_EQUAL(input3A * inputIdentity4x4, input3A);


    mat4<F32> inputIdentity4x4f;
    inputIdentity4x4f.identity();

    const mat4<F32> input4A({24.300f, 92.000f,  1.200f, 4.300f,
                              5.000f,  0.000f, 95.500f, 0.200f,
                             43.000f,  2.110f,  9.000f, 9.200f,
                              5.000f,  7.000f, 11.000f, 6.435f});

    const mat4<F32> input4B({ 12.500f,  2.000f,  6.000f, 8.400f,
                              23.000f, 29.340f, 11.000f, 5.120f,
                             -64.500f, 22.000f,  3.200f, 8.000f,
                              -2.000f,  5.000f,  7.000f, 1.000f });

    const mat4<F32> result4A({ 2333.750f, 2795.780f, 1191.740f, 689.060f,
                              -6097.650f, 2112.000f,  337.000f, 806.200f,
                               -12.870f,  391.907f,  374.410f, 453.203f,
                               -498.869f,  489.554f,  187.243f, 172.275f });

    const mat4<F32> result4B({  613.750f,  1221.460f,   352.400f,  163.401f,
                               1204.200f,  2175.050f,  2984.890f,  238.914f,
                              -1279.750f, -5871.248f,  2140.400f, -192.032f,
                                282.400f,  -162.230f,   549.100f,   63.235f});

    // Use compare instead of == as rounding errors bellow a certain threshold aren't as important with floating point matrices
    CHECK_TRUE(result4A.compare(input4A * input4B, 0.02f));
    CHECK_FALSE(result4A.compare(input4B * input4A, 0.02f));

    CHECK_FALSE(result4B.compare(input4A * input4B, 0.02f));
    CHECK_TRUE(result4B.compare(input4B * input4A, 0.02f));

    CHECK_TRUE(input4A.compare(input4A * inputIdentity4x4f, 0.02f));
}

TEST_MEMBER_FUNCTION(matN, construct, NA)
{
    const mat4<F32> input4A({ 24.300f, 92.000f,  1.200f, 4.300f,
                              5.000f,  0.000f, 95.500f, 0.200f,
                             43.000f,  2.110f,  9.000f, 9.200f,
                              5.000f,  7.000f, 11.000f, 6.435f });

    const mat4<F32> resultA(&input4A.mat[0]);
    const mat4<F32> resultB({input4A.mat[0], input4A.mat[1], input4A.mat[2], input4A.mat[3],
                             input4A.mat[4], input4A.mat[5], input4A.mat[6], input4A.mat[7],
                             input4A.mat[8], input4A.mat[9], input4A.mat[10], input4A.mat[11],
                             input4A.mat[12], input4A.mat[13], input4A.mat[14], input4A.mat[15]});
                
    CHECK_EQUAL(input4A, resultA);
    CHECK_EQUAL(input4A, resultB);
}

TEST_MEMBER_FUNCTION(matN, inverse, NA)
{
    //Floating point 2x2 inversion
    mat2<F32> input2x2(4.0f, 3.0f,
                       3.0f, 2.0f);
    const mat2<F32> rezult2x2(-2.0f,  3.0f,
                               3.0f, -4.0f);
    input2x2.inverse();
    CHECK_EQUAL(input2x2, rezult2x2);

    //Floating point 3x3 inversion
    mat3<F32> input3x3(2.0f, 1.0f, 0.0f,
                       0.0f, 2.0f, 0.0f,
                       2.0f, 0.0f, 1.0f);
    const mat3<F32> rezult3x3( 0.5f, -0.25f, 0.0f,
                               0.0f,  0.5f,  0.0f,
                              -1.0f,  0.5f,  1.0f);
    input3x3.inverse();
    CHECK_EQUAL(input3x3, rezult3x3);

    //Zero determinate inversion (F32 version is undefined)
    mat4<I32> zeroInput(0.0f);
    const mat4<I32> zeroResult(zeroInput);
    zeroInput.inverse();

    CHECK_EQUAL(zeroInput, zeroResult);

    //Floating point 4x4 inversion
    mat4<F32> input1({1.0f, 1.0f, 0.0f, 0.0f,
                      1.0f, 1.0f, 1.0f, 0.0f,
                      0.0f, 1.0f, 1.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 1.0f});
    const mat4<F32> result1({ 0.0f,  1.0f, -1.0f, 0.0f,
                              1.0f, -1.0f,  1.0f, 0.0f,
                             -1.0f,  1.0f,  0.0f, 0.0f,
                              0.0f,  0.0f,  0.0f, 1.0f});

    input1.inverse();
    CHECK_EQUAL(input1, result1);


    //Integer 4x4 inversion
    mat4<I32> input2({ 1, 1, 0, 0,
                      1, 1, 1, 0,
                      0, 1, 1, 0,
                      0, 0, 0, 1 });
    const mat4<I32> result2({ 0,  1, -1, 0,
                              1, -1,  1, 0,
                             -1,  1,  0, 0,
                              0,  0,  0, 1 });
    input2.inverse();
    CHECK_EQUAL(input2, result2);
}

} //namespace Divide