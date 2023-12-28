python compile_glsl.py --out-dir .\compiled_shaders\compute --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" --shader-stage=comp src\shaders\compute\read_depth.glsl
python compile_glsl.py --out-dir .\compiled_shaders\compute --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" --shader-stage=comp src\shaders\compute\reset_buffers.glsl
python compile_glsl.py --out-dir .\compiled_shaders\compute --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" --shader-stage=comp src\shaders\compute\copy_quantized_positions.glsl
python compile_glsl.py --out-dir .\compiled_shaders\compute --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" --shader-stage=comp src\shaders\compute\generate_shadow_matrices.glsl

python compile_glsl.py --out-dir .\compiled_shaders --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" .\src\shaders\cull_instances.comp
python compile_glsl.py --out-dir .\compiled_shaders --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" .\src\shaders\display_transform.comp
python compile_glsl.py --out-dir .\compiled_shaders --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" .\src\shaders\rasterization.glsl
python compile_glsl.py --out-dir .\compiled_shaders --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" .\src\shaders\render_geometry.comp
python compile_glsl.py --out-dir .\compiled_shaders --flags "-Werror -O -DGLSL=1 -std=450 --target-env=vulkan1.3 --target-spv=spv1.6 -I src" .\src\shaders\write_draw_calls.comp
