error_flags='-Wall -Wextra -Weverything -Wpedantic -Werror -Wno-c++98-compat -Wno-unused-macros -Wno-missing-variable-declarations -Wno-missing-prototypes -Wno-shorten-64-to-32'

for file in src/shaders/*.hlsl; do
    dxc -HV 2021 $error_flags -I src -spirv -fspv-target-env=vulkan1.1 -T lib_6_7 -enable-16bit-types $file -Fo compiled_shaders/$(basename $file .hlsl).spv || exit 1
done
