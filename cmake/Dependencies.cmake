include(FetchContent)

find_package(glfw3 CONFIG QUIET)
if(NOT TARGET glfw)
  message(STATUS "glfw not found on the system; fetching 3.4")
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()

find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
  message(STATUS "glm not found on the system; fetching 1.0.1")
  FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)
  set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
  set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glm)
endif()
