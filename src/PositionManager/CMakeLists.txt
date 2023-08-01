
add_library(PositionManager
        PositionManager.cpp,
        lwgps/src/lwgps/lwgps.c
)

target_link_libraries(PositionManager
	PUBLIC
		qgc
)

target_include_directories(PositionManager
	PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR},
                lwgps/src/include
	)

