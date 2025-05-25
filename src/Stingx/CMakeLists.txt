cmake_minimum_required(VERSION 3.10)

project(lstingX LANGUAGES CXX)

find_library(Z3_LIBRARY NAMES z3)
find_path(Z3_INCLUDE_DIR z3++.h)

find_library(PPL_LIBRARY NAMES ppl)
find_library(GMPXX_LIBRARY NAMES gmpxx)
find_library(GMP_LIBRARY NAMES gmp)

find_path(PPL_INCLUDE_DIR ppl.hh)
include_directories(${PPL_INCLUDE_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")
# Add source files
set(SRC_FILES
    ${BISON_LinTSParser_OUTPUTS}
    ${FLEX_LinTSLexer_OUTPUTS}
    Clump.cpp
    Context.cpp
    DisequalityStore.cpp
    Expression.cpp
    ExpressionStore.cpp
    funcs.cpp
    LinExpr.cpp
    LinTransform.cpp
    Location.cpp
    MatrixStore.cpp
    PolyStore.cpp
    PolyUtils.cpp
    Rational.cpp
    LinTS.cpp
    SparseLinExpr.cpp
    Timer.cpp
    TransitionRelation.cpp
    var-info.cpp
    Elimination.cpp
    Tree.cpp
    ToolUtils.cpp
    Macro.cpp
    lstingx.cpp
)


# create static library target
add_library(lstingx STATIC ${SRC_FILES})
target_link_libraries(lstingx PUBLIC  ${PPL_LIBRARY} ${GMPXX_LIBRARY} ${GMP_LIBRARY} ${Z3_LIBRARY})
target_compile_options(lstingx PRIVATE -g -Wno-deprecated-declarations)