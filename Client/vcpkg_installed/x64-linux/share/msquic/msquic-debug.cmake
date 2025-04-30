#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "msquic" for configuration "Debug"
set_property(TARGET msquic APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(msquic PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/libmsquic.so.2.4.8"
  IMPORTED_SONAME_DEBUG "libmsquic.so.2"
  )

list(APPEND _cmake_import_check_targets msquic )
list(APPEND _cmake_import_check_files_for_msquic "${_IMPORT_PREFIX}/debug/lib/libmsquic.so.2.4.8" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
