# Writes shared/qcommon/q_version.h with the output of git describe. Included by CMakeLists.txt when the build is
# configured and run as a script (cmake -P) by the target vjk_version at every build, so the version is the one of
# the commit that is built, not the one of the last configure. configure_file writes only when the text changes:
# a build without a new commit or tag recompiles nothing.
# Variables: VERSION_SOURCE_DIR, VERSION_BINARY_DIR, SOURCE_DATE, BUILD_PORTABLE

execute_process(
	COMMAND "git" "describe" "--tag"
	WORKING_DIRECTORY "${VERSION_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_TAG
	RESULT_VARIABLE GIT_TAG_ERROR
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT GIT_TAG_ERROR EQUAL 0)
	message(WARNING "Unable to determine Git tag")
	set(GIT_TAG vUNKNOWN)
endif()

configure_file("${VERSION_SOURCE_DIR}/shared/qcommon/q_version.h.in"
	"${VERSION_BINARY_DIR}/shared/qcommon/q_version.h" @ONLY)
