CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
#
# windows:  cmake . -G"NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
# 
# others:   cmake . -DCMAKE_BUILD_TYPE=Release
#
Project (X3F_tools)

FIND_PACKAGE(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

FIND_PACKAGE(OPENCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
link_directories(${OpenCV_LIBRARY_DIRS})

FIND_PACKAGE(ZLIB REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})
link_directories(${ZLIB_LIBRARY_DIRS})

FIND_PACKAGE(TIFF REQUIRED)
include_directories(${TIFF_INCLUDE_DIRS})
link_directories(${TIFF_LIBRARY_DIRS})

FIND_PACKAGE(ICONV REQUIRED)
include_directories(${ICONV_INCLUDE_DIRS})
link_directories(${ICONV_LIBRARY_DIRS})

FIND_PACKAGE(TBB)
IF (TBB_INCLUDE_DIRS)
  include_directories(${TBB_INCLUDE_DIRS})
  link_directories(${TBB_LIBRARY_DIRS})
ENDIF (TBB_INCLUDE_DIRS)

EXECUTE_PROCESS( COMMAND "git" "describe" "--always" "--dirty" "--tags" OUTPUT_VARIABLE "VERSION_OUTPUT" )
Message(STATUS, ${VERSION_OUTPUT})
string(REPLACE "-" ";" VERSION_STRING ${VERSION_OUTPUT})
list(GET VERSION_STRING 0 VERSION)
add_definitions(-DVERSION=\"${VERSION}\")

IF (WIN32)
  SET (AUXC "src/mingw_dowildcard.c")
ELSE (WIN32)
  SET (AUXC "")
ENDIF (WIN32)

IF (APPLE)
  SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl -dead_strip_dylibs -framework OpenCL")
ENDIF (APPLE)

SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O3 -Wall")

ADD_EXECUTABLE(x3f_extract 
  src/x3f_extract.cpp src/x3f_version.c src/x3f_io.c src/x3f_process.c src/x3f_meta.c src/x3f_image.c
  src/x3f_spatial_gain.c src/x3f_output_dng.c src/x3f_output_tiff.c src/x3f_output_ppm.c
  src/x3f_histogram.c src/x3f_print_meta.c src/x3f_dump.c src/x3f_matrix.c
  src/x3f_dngtags.c src/x3f_denoise_utils.cpp src/x3f_denoise_aniso.cpp
  src/x3f_denoise.cpp src/x3f_printf.c ${AUXC}
)

target_link_libraries(x3f_extract boost_program_options)
target_link_libraries(x3f_extract opencv_core opencv_photo opencv_imgproc)
target_link_libraries(x3f_extract tiff)
target_link_libraries(x3f_extract z)
target_link_libraries(x3f_extract iconv)
IF (TBB_INCLUDE_DIRS)
  target_link_libraries(x3f_extract tbb)
ENDIF (TBB_INCLUDE_DIRS)
