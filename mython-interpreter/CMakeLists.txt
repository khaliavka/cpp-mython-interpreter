cmake_minimum_required(VERSION 3.5)

project(mython-interpreter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HDRS lexer.h parse.h runtime.h statement.h test_runner_p.h)
set(SRCS main.cpp lexer.cpp lexer_test_open.cpp parse.cpp parse_test.cpp runtime.cpp runtime_test.cpp statement.cpp statement_test.cpp)
add_executable(interpreter ${HDRS} ${SRCS})

install(TARGETS interpreter
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
