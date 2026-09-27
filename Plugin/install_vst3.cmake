#
# Copy the built VST3 into the folder a host scans, and do not fail the build if it cannot.
#
# A DAW holds a plugin's binary open for as long as a set using it is loaded, so a rebuild
# while the host is running cannot replace the installed copy. That is ordinary - every
# plugin developer closes the host to rebuild - but JUCE's COPY_PLUGIN_AFTER_BUILD turns it
# into thirty lines of MSB3073 that bury the one sentence worth reading, and marks a build
# as failed when the build was fine and only the copy was not.
#
# So this does the copy and says plainly what happened. Invoked with -DSRC= and -DDST=.
#
if(NOT EXISTS "${SRC}")
    message(STATUS "Mz950: nothing to install - ${SRC} does not exist")
    return()
endif()

get_filename_component(BUNDLE "${SRC}" NAME)

#
# The plugin was called VirtualS950 until it was renamed Mz950. The old bundle carries the
# SAME plugin ID - the ID comes from the codes, which did not change - so leaving it beside
# the new one gives a host two copies of one plugin in one folder, and which it loads is
# anybody's guess. Take it away. If a host is holding it open this fails quietly, and the
# next build with the host closed finishes the job.
#
if(EXISTS "${DST}/VirtualS950.vst3" AND NOT BUNDLE STREQUAL "VirtualS950.vst3")
    file(REMOVE_RECURSE "${DST}/VirtualS950.vst3")
    if(EXISTS "${DST}/VirtualS950.vst3")
        message(STATUS "Mz950: the old VirtualS950.vst3 is still in ${DST} - close the host and build again")
    else()
        message(STATUS "Mz950: removed the old VirtualS950.vst3 from ${DST}")
    endif()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SRC}" "${DST}/${BUNDLE}"
    RESULT_VARIABLE COPY_RESULT
    ERROR_VARIABLE  COPY_ERROR
    OUTPUT_QUIET)

if(COPY_RESULT EQUAL 0)
    message(STATUS "Mz950: installed to ${DST}/${BUNDLE}")
else()
    message(STATUS "")
    message(STATUS "  Mz950: the plugin BUILT but could not be INSTALLED.")
    message(STATUS "  Your host almost certainly has the old one loaded and is holding it open.")
    message(STATUS "  Close the DAW and build again - nothing is wrong with the build itself.")
    message(STATUS "")
    message(STATUS "    wanted: ${DST}/${BUNDLE}")
    message(STATUS "")
endif()
