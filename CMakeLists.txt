#
# You rarely need to edit this file. To add or remove files in your
# project edit the "src" folders contents and run `mulle-sde reflect`
#
#
# A convenient way to add a new source file is:
#    mulle-sde add src/foo.c
#
cmake_minimum_required( VERSION 3.13...99.99)

project( mulle-mmapallocator VERSION 0.1.1 LANGUAGES C)


set( MULLE_PROJECT_TYPE "Library")

### mulle-sde environment

# add cmake module paths to search path
list( INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/share")
list( INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/reflect")
list( INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include( Environment)

include( Files)


### Library

if( HEADER_PHASE)
   install( FILES ${INSTALL_PUBLIC_HEADERS}  DESTINATION "include/${PROJECT_NAME}")
   install( FILES ${INSTALL_PRIVATE_HEADERS} DESTINATION "include/${PROJECT_NAME}")
   install( FILES ${INSTALL_CMAKE_INCLUDES}          DESTINATION "include/${PROJECT_NAME}/cmake")

   # short cut out
   if( NOT COMPILE_PHASE AND NOT LINK_PHASE)
      return()
   endif()
endif()

if( LINK_PHASE)
   include( Dependencies OPTIONAL)
endif()

include( Library)


target_compile_definitions( "${LIBRARY_COMPILE_TARGET}"
PUBLIC
   MSPACES
   ONLY_MSPACES      # will set HAVE_MORECORE to 0, which means mmap is used
   NO_MALLINFO
)

target_compile_options( "${LIBRARY_COMPILE_TARGET}"
PUBLIC
   -Wall
   -Wno-unused-function
)

include( InstallLibrary)

if( LINK_PHASE)
   include( InstallCMakeInclude)
   include( InstallCMakePackage)
endif() 

include( FinalOutput OPTIONAL)


# extension : mulle-sde/cmake
# directory : project-oneshot/library
# template  : .../CMakeLists.txt
# Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
