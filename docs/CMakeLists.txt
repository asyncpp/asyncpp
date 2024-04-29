find_package(Git)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags RESULT_VARIABLE GIT_DESCRIBE_RESULT
                  OUTPUT_VARIABLE GIT_DESCRIBE_OUTPUT WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT GIT_DESCRIBE_RESULT EQUAL 0)
    message(AUTHOR_WARNING "Could not detect project version, using \"unknown\"")
    set(GIT_DESCRIBE_OUTPUT "unknown")
  endif()
else()
  message(STATUS "Could not find git, project version set to \"unknown\"")
  set(GIT_DESCRIBE_OUTPUT "unknown")
endif()
message(STATUS "Project version: ${GIT_DESCRIBE_OUTPUT}")

file(DOWNLOAD https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/v2.3.2/doxygen-awesome.css
     ${CMAKE_CURRENT_BINARY_DIR}/doxygen-awesome.css
     EXPECTED_HASH SHA256=4c4266210c340dffeb9f3398bf9521bb5f3a25680bb9e6f3d73f7e1f32fc7a7d)
file(DOWNLOAD https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/v2.3.2/doxygen-awesome-sidebar-only.css
     ${CMAKE_CURRENT_BINARY_DIR}/doxygen-awesome-sidebar-only.css
     EXPECTED_HASH SHA256=998328b27193b7be007a431bc9be1a6f6855ff4d8fa722ecfdfed79a8931409f)
file(DOWNLOAD https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/v2.3.2/doxygen-awesome-darkmode-toggle.js
     ${CMAKE_CURRENT_BINARY_DIR}/doxygen-awesome-darkmode-toggle.js
     EXPECTED_HASH SHA256=931ed337831ecaa5b26105cbbdd1f3077f210ec44d5081e6647f4d98e18bddd3)
file(DOWNLOAD https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/v2.3.2/doxygen-awesome-sidebar-only-darkmode-toggle.css
     ${CMAKE_CURRENT_BINARY_DIR}/doxygen-awesome-sidebar-only-darkmode-toggle.css
     EXPECTED_HASH SHA256=b1907142f53b2350507fa5e42ce72f2213c680b2e3482458d3def031c364b150)
     file(DOWNLOAD https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/v2.3.2/doxygen-awesome-fragment-copy-button.js
          ${CMAKE_CURRENT_BINARY_DIR}/doxygen-awesome-fragment-copy-button.js
          EXPECTED_HASH SHA256=228d13ae7407c705899b415763e8e6e3d6c3c475a60e0b99b08857c41428cb1c)

execute_process(COMMAND doxygen --version RESULT_VARIABLE DOXYGEN_RESULT
                OUTPUT_VARIABLE DOXYGEN_VERSION_RAW_OUTPUT)
if(NOT DOXYGEN_RESULT EQUAL 0)
  message(AUTHOR_WARNING "Unable to get the version of doxygen")
else()
  # Extracts the version from the output of the command run before
  string(REGEX MATCH "^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)" DOXYGEN_VERSION_OUTPUT
               "${DOXYGEN_VERSION_RAW_OUTPUT}")
  message(STATUS "doxygen version detected : ${DOXYGEN_VERSION_OUTPUT}")
endif()

execute_process(COMMAND doxygen -w html header.html footer.html doxygen.css 
                RESULT_VARIABLE DOXYGEN_RESULT OUTPUT_QUIET
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
if(NOT DOXYGEN_RESULT EQUAL 0)
  message(AUTHOR_WARNING "Failed to retrieve default doxygen template")
endif()
file(READ ${CMAKE_CURRENT_BINARY_DIR}/header.html HEADER_CONTENTS)
string(REPLACE "</head>" "<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-darkmode-toggle.js\"></script>
<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-fragment-copy-button.js\"></script>
<script type=\"text/javascript\">
  DoxygenAwesomeDarkModeToggle.init();
  DoxygenAwesomeFragmentCopyButton.init();
</script>
</head>" HEADER_CONTENTS ${HEADER_CONTENTS})
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/header.html ${HEADER_CONTENTS})

configure_file(${CMAKE_CURRENT_LIST_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

add_custom_target(asyncpp-docs
  COMMAND doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  SOURCES ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
  BYPRODUCTS ${PROJECT_BINARY_DIR}/html/index.html)
add_custom_command(
  TARGET asyncpp-docs POST_BUILD COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan
                                     "You can prview the documentation: ${PROJECT_BINARY_DIR}/html/index.html")