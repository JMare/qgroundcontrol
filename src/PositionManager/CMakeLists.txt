
add_library(PositionManager
	PositionManager.cpp
)

target_link_libraries(PositionManager
	PUBLIC
		qgc
)

target_include_directories(PositionManager
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
	)

