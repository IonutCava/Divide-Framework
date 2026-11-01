# Copyright (c) 2026 DIVIDE-Studio
function(divide_jolt_simd_options features architecture output)
    set(sse OFF)
    set(avx OFF)
    set(avx2 OFF)
    set(avx512 OFF)
    if(architecture MATCHES "^(x86|x64)$")
        set(sse ON)
        if("avx512" IN_LIST features)
            set(avx512 ON)
            set(avx2 ON)
            set(avx ON)
        elseif("avx2" IN_LIST features)
            set(avx2 ON)
            set(avx ON)
        elseif("avx" IN_LIST features)
            set(avx ON)
        endif()
    endif()
    set(${output}
        -DUSE_SSE4_1=${sse} -DUSE_SSE4_2=${sse}
        -DUSE_AVX=${avx} -DUSE_AVX2=${avx2} -DUSE_AVX512=${avx512}
        -DUSE_LZCNT=${avx2} -DUSE_TZCNT=${avx2}
        -DUSE_F16C=${avx2} -DUSE_FMADD=${avx2}
        PARENT_SCOPE)
endfunction()
