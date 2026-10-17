function(repodoctor_enable_sanitizers target_name)
    if(REPODOCTOR_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(
                ${target_name}
                INTERFACE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
            )
            target_link_options(
                ${target_name}
                INTERFACE
                    -fsanitize=address,undefined
            )
        endif()
    endif()
endfunction()
