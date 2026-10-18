# Our changes to melonDS (third_party/melonds, pinned to upstream 1.1) live in melonds-pokedaisy.patch: EXPAND's hooks
# in GPU3D (every polygon past culling, the lists' swap at VBlank, the clipper), the software renderer's column range,
# fast-forward's frame skip (GPU::SkipDraw: no 2D lines or 3D render on a frame no one sees)
# and NDS::Current as a plain static (Android's emulated thread_local cost ~8% of the emulation thread).
# Applied to the submodule's working tree at configure time - once: a tree that already has it is left alone - so a
# fresh checkout builds as is. Moving the submodule means re-making the patch (git -C third_party/melonds diff).
#   include(.../apply_melonds_patch.cmake) after setting MELONDS_DIR
set(PK_MELONDS_PATCH "${CMAKE_CURRENT_LIST_DIR}/melonds-pokedaisy.patch")
execute_process(COMMAND git apply --reverse --check "${PK_MELONDS_PATCH}"
    WORKING_DIRECTORY "${MELONDS_DIR}" RESULT_VARIABLE PK_MELONDS_PATCHED OUTPUT_QUIET ERROR_QUIET)
if(NOT PK_MELONDS_PATCHED EQUAL 0)
    execute_process(COMMAND git apply "${PK_MELONDS_PATCH}"
        WORKING_DIRECTORY "${MELONDS_DIR}" RESULT_VARIABLE PK_MELONDS_APPLY ERROR_VARIABLE PK_MELONDS_ERR)
    if(NOT PK_MELONDS_APPLY EQUAL 0)
        message(FATAL_ERROR "melonDS patch didn't apply (${PK_MELONDS_PATCH}): ${PK_MELONDS_ERR}")
    endif()
    message(STATUS "melonDS: applied melonds-pokedaisy.patch")
endif()
