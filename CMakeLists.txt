cmake_minimum_required(VERSION 3.5.1)
project (VulkanTutorial)
set(TARGET_NAME VulkanTutorial)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

find_library(VULKAN vulkan /Users/sichaoshu/VulkanSDK/1.3.268.1/macOS/lib)

add_subdirectory(thirdparty)

add_executable(${TARGET_NAME} main.cpp)

target_link_libraries(${TARGET_NAME} PRIVATE stb)
target_link_libraries(${TARGET_NAME} PRIVATE tiny)
target_link_libraries(${TARGET_NAME} PRIVATE glm)
target_link_libraries(${TARGET_NAME} PRIVATE glfw)
target_link_libraries(${TARGET_NAME} PRIVATE ${VULKAN})
target_link_libraries(${TARGET_NAME} PRIVATE tinygltf)

target_include_directories(${TARGET_NAME} PRIVATE /Users/sichaoshu/VulkanSDK/1.3.268.1/macOS/include)