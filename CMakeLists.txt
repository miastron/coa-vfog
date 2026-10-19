cmake_minimum_required(VERSION 3.20)
project(coa_vfog CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "coa-vfog targets the 32-bit client; configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

file(GLOB FXC_CANDIDATES "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/10.*/x86/fxc.exe")
list(SORT FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
list(GET FXC_CANDIDATES 0 FXC_DEFAULT)
set(FXC "${FXC_DEFAULT}" CACHE FILEPATH "HLSL compiler (fxc.exe)")
if(NOT EXISTS "${FXC}")
    message(FATAL_ERROR "fxc.exe not found; install the Windows SDK or set -DFXC=<path>")
endif()

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(smaa
    URL https://github.com/iryoku/smaa/archive/71c806a838bdd7d517df19192a20f0c61b3ca29d.tar.gz
    URL_HASH SHA256=046a435f5713f8bfe8148fe6305c2abf1409fc2122bee6070061c675e1e55f57)
FetchContent_MakeAvailable(smaa)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

set(VF_INCLUDES ${SHADER_DIR}/vf_common.hlsli ${SHADER_DIR}/vf_integrate.hlsli
                ${SHADER_DIR}/vf_local_lights.hlsli ${SHADER_DIR}/vf_local_light_data.hlsli
                ${SHADER_DIR}/vf_density_variation.hlsli
                ${SHADER_DIR}/vf_authored_noise.hlsli ${SHADER_DIR}/vf_composite.hlsli
                ${SHADER_DIR}/vf_sample_split.hlsli
                ${SHADER_DIR}/vf_sun_shadows.hlsli)
set(VW_FFT_INCLUDES ${SHADER_DIR}/vw_fft_common.hlsli)
set(VW_INCLUDES ${SHADER_DIR}/vf_common.hlsli ${SHADER_DIR}/vw_constants.hlsli ${SHADER_DIR}/vw_surface.hlsli
                ${SHADER_DIR}/vw_optics.hlsli ${SHADER_DIR}/vw_reflection.hlsli)

set(SHADER_HEADERS)
function(vf_shader name source profile)
    if(source MATCHES "^vw_fft_")
        set(includes ${VW_FFT_INCLUDES})
    elseif(source MATCHES "^vw_")
        set(includes ${VW_INCLUDES})
    elseif(source MATCHES "^vp_bloom_")
        set(includes ${SHADER_DIR}/vp_image.hlsli ${SHADER_DIR}/vp_bloom.hlsli)
    elseif(source MATCHES "^vp_smaa_")
        set(includes ${SHADER_DIR}/vp_image.hlsli ${SHADER_DIR}/vp_smaa.hlsli ${smaa_SOURCE_DIR}/SMAA.hlsl)
    elseif(source MATCHES "^vp_")
        set(includes ${SHADER_DIR}/vp_image.hlsli)
    else()
        set(includes ${VF_INCLUDES})
    endif()
    set(out ${SHADER_OUT}/${name}.h)
    add_custom_command(
        OUTPUT ${out}
        COMMAND "${FXC}" /nologo /O3 /T ${profile} /E main /Vn g_${name} ${ARGN}
                /Fh ${out} /Fc ${SHADER_OUT}/${name}.asm ${SHADER_DIR}/${source}
        DEPENDS ${SHADER_DIR}/${source} ${includes}
        COMMENT "fxc ${name}"
        VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${out} PARENT_SCOPE)
endfunction()

vf_shader(vs_fullscreen vf_fullscreen_vs.hlsl vs_3_0)
vf_shader(ps_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0)
vf_shader(ps_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0)
vf_shader(ps_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0)
vf_shader(ps_lit_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1)
vf_shader(ps_lit_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1)
vf_shader(ps_lit_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1)
vf_shader(ps_noisy_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_noisy_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_noisy_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_lit_noisy_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_lit_noisy_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_lit_noisy_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1 /DSAMPLES_AUTHORED_NOISE=1)
set(SHADOWS_LOW /DSUN_SHADOWS=1 /DSHADOW_STEP_STRIDE=4 /DSHADOW_TRACE_TAPS=12)
set(SHADOWS_MID /DSUN_SHADOWS=1 /DSHADOW_STEP_STRIDE=3 /DSHADOW_TRACE_TAPS=12)
set(SHADOWS_HIGH /DSUN_SHADOWS=1 /DSHADOW_STEP_STRIDE=2 /DSHADOW_TRACE_TAPS=12)
set(SHADOWS_EVERY_STEP_LOW /DSUN_SHADOWS=1 /DSHADOW_STEP_STRIDE=1 /DSHADOW_TRACE_TAPS=12)
set(SHADOWS_EVERY_STEP_MID /DSUN_SHADOWS=1 /DSHADOW_STEP_STRIDE=1 /DSHADOW_TRACE_TAPS=12)
set(SHADOWS_EVERY_STEP_HIGH /DSUN_SHADOWS=1 /DSHADOW_STEP_STRIDE=1 /DSHADOW_TRACE_TAPS=12)
vf_shader(ps_shadowed_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0 ${SHADOWS_LOW})
vf_shader(ps_shadowed_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0 ${SHADOWS_MID})
vf_shader(ps_shadowed_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0 ${SHADOWS_HIGH})
vf_shader(ps_shadowed_noisy_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1
          ${SHADOWS_LOW})
vf_shader(ps_shadowed_noisy_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1
          ${SHADOWS_MID})
vf_shader(ps_shadowed_noisy_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1
          ${SHADOWS_HIGH})
vf_shader(ps_shadowed_lit_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1 ${SHADOWS_LOW})
vf_shader(ps_shadowed_lit_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1 ${SHADOWS_MID})
vf_shader(ps_shadowed_lit_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1 ${SHADOWS_HIGH})
vf_shader(ps_shadowed_lit_noisy_march_low vf_march.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1 /DSAMPLES_AUTHORED_NOISE=1
          ${SHADOWS_EVERY_STEP_LOW})
vf_shader(ps_shadowed_lit_noisy_march_mid vf_march.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1 /DSAMPLES_AUTHORED_NOISE=1
          ${SHADOWS_EVERY_STEP_MID})
vf_shader(ps_shadowed_lit_noisy_march_high vf_march.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1 /DSAMPLES_AUTHORED_NOISE=1
          ${SHADOWS_EVERY_STEP_HIGH})
vf_shader(ps_density_probe vf_density_probe.hlsl ps_3_0)
vf_shader(ps_temporal vf_temporal.hlsl ps_3_0)
vf_shader(ps_history_depth vf_history_depth.hlsl ps_3_0)
vf_shader(ps_composite_low vf_composite.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0)
vf_shader(ps_composite_mid vf_composite.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0)
vf_shader(ps_composite_high vf_composite.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0)
vf_shader(ps_noisy_composite_low vf_composite.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_noisy_composite_mid vf_composite.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_noisy_composite_high vf_composite.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0 /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_lit_composite_low vf_composite.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1)
vf_shader(ps_lit_composite_mid vf_composite.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1)
vf_shader(ps_lit_composite_high vf_composite.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1)
vf_shader(ps_shadowed_composite_low vf_composite.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1 ${SHADOWS_LOW})
vf_shader(ps_shadowed_composite_mid vf_composite.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1 ${SHADOWS_MID})
vf_shader(ps_shadowed_composite_high vf_composite.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1 ${SHADOWS_HIGH})
vf_shader(ps_split_composite_low vf_composite_split.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0)
vf_shader(ps_split_composite_mid vf_composite_split.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0)
vf_shader(ps_split_composite_high vf_composite_split.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0)
vf_shader(ps_noisy_split_composite_low vf_composite_split.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0
          /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_noisy_split_composite_mid vf_composite_split.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=0
          /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_noisy_split_composite_high vf_composite_split.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=0
          /DSAMPLES_AUTHORED_NOISE=1)
vf_shader(ps_lit_split_composite_low vf_composite_split.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1)
vf_shader(ps_lit_split_composite_mid vf_composite_split.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1)
vf_shader(ps_lit_split_composite_high vf_composite_split.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1)
vf_shader(ps_shadowed_split_composite_low vf_composite_split.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=1 ${SHADOWS_LOW})
vf_shader(ps_shadowed_split_composite_mid vf_composite_split.hlsl ps_3_0 /DSTEPS=24 /DLOCAL_LIGHTS=1 ${SHADOWS_MID})
vf_shader(ps_shadowed_split_composite_high vf_composite_split.hlsl ps_3_0 /DSTEPS=32 /DLOCAL_LIGHTS=1
          ${SHADOWS_HIGH})
vf_shader(ps_silhouette_mask vf_silhouette_mask.hlsl ps_3_0 /DLOCAL_LIGHTS=0)
vf_shader(ps_ray_mask vf_ray_mask.hlsl ps_3_0)
vf_shader(ps_ray_blur vf_ray_blur.hlsl ps_3_0)
vf_shader(ps_ray_composite vf_ray_composite.hlsl ps_3_0 /DSTEPS=16 /DLOCAL_LIGHTS=0)
vf_shader(ps_probe vf_probe.hlsl ps_3_0)
vf_shader(ps_surface_lights vf_surface_lights.hlsl ps_3_0)
vf_shader(ps_depth_check vf_depth_check.hlsl ps_3_0)
vf_shader(ps_grade vp_grade.hlsl ps_3_0)
vf_shader(ps_sharpen vp_sharpen.hlsl ps_3_0)
vf_shader(ps_bloom_prefilter vp_bloom_prefilter.hlsl ps_3_0)
vf_shader(ps_bloom_down vp_bloom_down.hlsl ps_3_0)
vf_shader(ps_bloom_up vp_bloom_up.hlsl ps_3_0)
vf_shader(ps_bloom_composite vp_bloom_composite.hlsl ps_3_0)
vf_shader(ps_smaa_edges vp_smaa_edges.hlsl ps_3_0 /I ${smaa_SOURCE_DIR})
vf_shader(ps_smaa_weights vp_smaa_weights.hlsl ps_3_0 /I ${smaa_SOURCE_DIR})
vf_shader(ps_smaa_blend vp_smaa_blend.hlsl ps_3_0 /I ${smaa_SOURCE_DIR})
vf_shader(vs_client_m2_fog ../tests/shaders/client_m2_fog_vs.hlsl vs_3_0)
vf_shader(ps_client_m2_fog ../tests/shaders/client_m2_fog_ps.hlsl ps_3_0)
include(cmake/WaterFftShaders.cmake)
include(cmake/WaterShaders.cmake)
add_custom_target(vf_shaders DEPENDS ${SHADER_HEADERS})

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui
    PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS
    PRIVATE IMGUI_IMPL_WIN32_DISABLE_GAMEPAD IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
            IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(CoAVolFog SHARED
    src/client_code.cpp
    src/client_light_stay.cpp
    src/client_ripple_sprites.cpp
    src/config.cpp
    src/d3d9_wrap.cpp
    src/dllmain.cpp
    src/engine.cpp
    src/engine_actors.cpp
    src/engine_lights.cpp
    src/engine_shadows.cpp
    src/fog_data.cpp
    src/fog_model.cpp
    src/forever_look.cpp
    src/gpu_timing.cpp
    src/grading_renderer.cpp
    src/hooks.cpp
    src/image_quality.cpp
    src/image_renderer.cpp
    src/local_light_stabiliser.cpp
    src/log.cpp
    src/msaa_depth.cpp
    src/noise_volume.cpp
    src/overlay.cpp
    src/renderer.cpp
    src/settings_panel.cpp
    src/spell_lights.cpp
    src/status_log.cpp
    src/texture_filtering.cpp
    src/texture_tint.cpp
    src/transparent_fog.cpp
    src/water_contacts.cpp
    src/water_data.cpp
    src/water_fft.cpp
    src/water_renderer.cpp
    src/water_ripples.cpp
    src/water_spectrum.cpp
    src/CoAVolFog.def)
add_dependencies(CoAVolFog vf_shaders)
target_include_directories(CoAVolFog PRIVATE src ${SHADER_OUT} ${smaa_SOURCE_DIR}/Textures)
target_compile_definitions(CoAVolFog PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(CoAVolFog PRIVATE /W4 /wd4100 /arch:SSE2 /fp:fast /GS)
target_link_libraries(CoAVolFog PRIVATE kernel32 user32 imgui)

add_library(version SHARED proxy/version_proxy.cpp proxy/version.def)
target_link_libraries(version PRIVATE CoAVolFog)
target_compile_definitions(version PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(vfog_harness tests/harness.cpp src/client_code.cpp src/client_light_stay.cpp
               src/client_ripple_sprites.cpp src/config.cpp src/engine_actors.cpp
               src/engine_lights.cpp src/local_light_stabiliser.cpp src/spell_lights.cpp src/texture_tint.cpp
               src/texture_filtering.cpp
               src/fog_data.cpp src/fog_model.cpp src/gpu_timing.cpp src/log.cpp src/noise_volume.cpp
               src/transparent_fog.cpp src/water_contacts.cpp src/water_data.cpp src/water_spectrum.cpp
               src/water_fft.cpp src/water_ripples.cpp src/status_log.cpp)
target_include_directories(vfog_harness PRIVATE src ${SHADER_OUT})
target_compile_definitions(vfog_harness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(vfog_harness PRIVATE CoAVolFog user32 ole32 windowscodecs)
add_custom_command(TARGET CoAVolFog POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/CoAVolFog.ini
            ${CMAKE_CURRENT_SOURCE_DIR}/data/fogdata.bin ${CMAKE_CURRENT_SOURCE_DIR}/data/waterdata.bin
            $<TARGET_FILE_DIR:CoAVolFog>)

enable_testing()
configure_file(CoAVolFog.ini interior-test.ini COPYONLY)
add_test(NAME interior_fog
    COMMAND vfog_harness --scene interior --ini ${CMAKE_CURRENT_BINARY_DIR}/interior-test.ini
    WORKING_DIRECTORY $<TARGET_FILE_DIR:vfog_harness>)
add_test(NAME vfog_harness
    COMMAND vfog_harness --out ${CMAKE_CURRENT_BINARY_DIR}/harness-out
            --data ${CMAKE_CURRENT_SOURCE_DIR}/data/fogdata.bin
            --ini ${CMAKE_CURRENT_SOURCE_DIR}/CoAVolFog.ini
            --water-data ${CMAKE_CURRENT_SOURCE_DIR}/data/waterdata.bin
    WORKING_DIRECTORY $<TARGET_FILE_DIR:vfog_harness>)
set_tests_properties(vfog_harness PROPERTIES LABELS gpu)
add_test(NAME cpu_harness
    COMMAND vfog_harness --scene cpu --out ${CMAKE_CURRENT_BINARY_DIR}/cpu-harness-out
            --data ${CMAKE_CURRENT_SOURCE_DIR}/data/fogdata.bin
            --ini ${CMAKE_CURRENT_SOURCE_DIR}/CoAVolFog.ini
            --water-data ${CMAKE_CURRENT_SOURCE_DIR}/data/waterdata.bin
    WORKING_DIRECTORY $<TARGET_FILE_DIR:vfog_harness>)
set_tests_properties(cpu_harness PROPERTIES LABELS cpu)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME comments
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_comments.py
                --root ${CMAKE_CURRENT_SOURCE_DIR})
endif()
